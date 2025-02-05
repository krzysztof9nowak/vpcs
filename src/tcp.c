/*
 * Copyright (c) 2007-2015, Paul Meng (mirnshi@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 * THE POSSIBILITY OF SUCH DAMAGE.
**/

#include <stdio.h>
#include <unistd.h> /* usleep */
#include <stdlib.h> /* random */
#include <string.h> /* string op */

#include "vpcs.h"
#include "tcp.h"
#include "packets.h"
#include "packets6.h"
#include "utils.h"

extern int pcid;
extern int ctrl_c;
extern u_int time_tick;
extern int dmpflag;

/*******************************************************
 *      client                  server
 *                 SYN  ->
 *                      <- SYN + ACK
 *                 ACk  ->
 *             (sseq+1)  
 *    ACK + PUSH + data ->
 *             sseq + 1
 *                      <- ACK 
 *                         cseq + sizeof(data)
 *                  FIN ->
 *                sseq+1   
 *         wait1
 *                      <- ACK
 *         wait2           sseq+1
 *                      <- FIN
 *                            close wait
 *                  ACk ->
 *******************************************************/
int tcp_ack(pcs *pc, int ipv)
{
	struct packet *m = NULL;
	
	struct packet * (*fpacket)(pcs *pc);
	
	if (ipv == IPV6_VERSION)
		fpacket = packet6;
	else
		fpacket = packet;
	
	pc->mscb.flags = TH_ACK;

	m = fpacket(pc);
	
	if (m == NULL) {
		printf("out of memory\n");
		return 0;
	}
	
	/* push m into the background output queue 
	   which is watched by pth_output */
	enq(&pc->bgoq, m);
	
	return 1;
}

int tcp_open(pcs *pc, int ipv)
{
	struct packet *m, *p;
	int i = 0, ok;
	int state = 0;
	struct packet * (*fpacket)(pcs *pc);
	int (*fresponse)(struct packet *pkt, sesscb *sesscb);
	
	if (ipv == IPV6_VERSION) {
		fpacket = packet6;
		fresponse = response6;
	} else {
		fpacket = packet;
		fresponse = response;
	}
	
	/* try to connect */
	while (i++ < 3 && ctrl_c == 0) {
		struct timeval tv;
		
		pc->mscb.flags = TH_SYN;
		pc->mscb.timeout = time_tick;
		pc->mscb.seq = rand();
		pc->mscb.ack = 0;

		m = fpacket(pc);
	
		if (m == NULL) {
			printf("out of memory\n");
			return 0;
		}
		/* push m into the background output queue 
		   which is watched by pth_output */
		enq(&pc->bgoq, m);
		
		//k = 0;
		ok = 0;
		gettimeofday(&(tv), (void*)0);
		while (!timeout(tv, pc->mscb.waittime) && !ctrl_c) {
			delay_ms(1);

			while ((p = deq(&pc->iq)) != NULL && 
			    !timeout(tv, pc->mscb.waittime) && !ctrl_c) {	
				
				ok = fresponse(p, &pc->mscb);
				del_pkt(p);

				if (!ok) 
					continue;

				if (ok == IPPROTO_ICMP)
					return 2;
				
				if (pc->mscb.rack == (pc->mscb.seq + 1) && 
					pc->mscb.rflags == (TH_SYN | TH_ACK)) {
					state = 1;
					tv.tv_sec = 0;
					break;
				} 
				
				if ((pc->mscb.rflags & TH_RST) != TH_RST) {
					pc->mscb.flags = TH_RST | TH_ACK;
					pc->mscb.seq = pc->mscb.rack;
					pc->mscb.ack = pc->mscb.rseq;
					
					m = fpacket(pc);
					if (m == NULL) {
						printf("out of memory\n");
						return 0;
					}
					/* push m into the background output 
					   queue which is watched 
					   by pth_output */
					enq(&pc->bgoq, m);
					
					delay_ms(1);
					tv.tv_sec = 0;
					break;
				}
				if ((pc->mscb.rflags & TH_RST) == TH_RST)
					return 3;	
				
				tv.tv_sec = 0;
				break;
			}
		}
		
		if (state != 1)
			continue;
		
		/* reply ACK , ack+1 */
		pc->mscb.seq = pc->mscb.rack;
		pc->mscb.ack = pc->mscb.rseq + 1;
		tcp_ack(pc, ipv);

		return 1;
	}
	return 0;
}
/*
 * return 1 if ACK, 2 if FIN|PUSH
 */
int tcp_send(pcs *pc, int ipv)
{
	struct packet *m, *p;
	int i = 0, ok;
	int state = 0;
	
	struct packet * (*fpacket)(pcs *pc);
	int (*fresponse)(struct packet *pkt, sesscb *sesscb);
	
	if (ipv == IPV6_VERSION) {
		fpacket = packet6;
		fresponse = response6;
	} else {
		fpacket = packet;
		fresponse = response;
	}
	
	/* drop the response if any, but update the ack */
	while ((p = deq(&pc->iq)) != NULL) {	
		ok = fresponse(p, &pc->mscb);
		del_pkt(p);
		
		if (!ok)
			continue;
			
		if (pc->mscb.rflags == (TH_ACK | TH_PUSH) &&
			pc->mscb.seq == pc->mscb.rack) {
			pc->mscb.ack = pc->mscb.rseq + pc->mscb.rdsize;
			tcp_ack(pc, ipv);
			delay_ms(1);
		}
	}	
	 
	/* try to send */ 
	while (i++ < 3 && ctrl_c == 0) {
		struct timeval tv;
		
		pc->mscb.flags = TH_ACK | TH_PUSH;
		m = fpacket(pc);
	
		if (m == NULL) {
			printf("out of memory\n");
			return 0;
		}
		/* push m into the background output queue 
		   which is watched by pth_output */
		enq(&pc->bgoq, m);
		
		//k = 0;
		ok = 0;
		gettimeofday(&(tv), (void*)0);
		while (!timeout(tv, pc->mscb.waittime) && !ctrl_c) {
			delay_ms(1);
			while ((p = deq(&pc->iq)) != NULL) {	
				ok = fresponse(p, &pc->mscb);
				del_pkt(p);

				if (!ok)
					continue;

				if (pc->mscb.rflags == (TH_ACK) && 
				    pc->mscb.rack == pc->mscb.seq + pc->mscb.dsize) {
					pc->mscb.seq = pc->mscb.rack;
					pc->mscb.ack = pc->mscb.rseq;		
					state = 1;
					
					return 1;
				}
				if (pc->mscb.rflags == (TH_ACK | TH_PUSH)) {
					u_int tseq;
					
					tseq = pc->mscb.seq;
					
					pc->mscb.seq = pc->mscb.rack;
					pc->mscb.ack = pc->mscb.rseq + pc->mscb.rdsize;
					
					tcp_ack(pc, ipv);
					
					if (pc->mscb.seq == tseq+ pc->mscb.dsize)
						return 1;
					else {
						delay_ms(1);
						continue;
					}
				}
					
				/* the remote does not like me, closing the connection */	
				if (pc->mscb.rflags == (TH_ACK | TH_PUSH | TH_FIN)) {
					pc->mscb.seq = pc->mscb.rack;
					pc->mscb.ack = pc->mscb.rseq + pc->mscb.rdsize;

					state = 1;
				
					return 2;
				}
				tv.tv_sec = 0;	
				break;
			}
		}
		
		if (state != 1)
			continue;
					
		return 1;
	}
	return 0;
}

int tcp_close(pcs *pc, int ipv)
{
	struct packet *m, *p;
	int i = 0, ok;
	int state = 0;
	int rfin = 0;
	
	struct packet * (*fpacket)(pcs *pc);
	int (*fresponse)(struct packet *pkt, sesscb *sesscb);
	
	if (ipv == IPV6_VERSION) {
		fpacket = packet6;
		fresponse = response6;
	} else {
		fpacket = packet;
		fresponse = response;
	}
	
	/* drop the response if any, but update the ack */
	while ((p = deq(&pc->iq)) != NULL) {	
		ok = fresponse(p, &pc->mscb);
		del_pkt(p);
		
		if (!ok)
			continue;
		
		if (pc->mscb.rflags == (TH_ACK | TH_PUSH) &&
			pc->mscb.seq == pc->mscb.rack) {
			pc->mscb.ack = pc->mscb.rseq + pc->mscb.rdsize;
			tcp_ack(pc, ipv);
			delay_ms(1);
			continue;
		}
		if (pc->mscb.rflags == TH_ACK) {
			pc->mscb.seq = pc->mscb.rack;
			pc->mscb.ack = pc->mscb.rseq;
			break;
		}
		if ((pc->mscb.rflags & (TH_ACK | TH_FIN)) == (TH_ACK | TH_FIN)) {
			pc->mscb.seq = pc->mscb.rack;
			pc->mscb.ack = pc->mscb.rseq;
			pc->mscb.ack++;
			
			tcp_ack(pc, ipv);
			
			delay_ms(1);
			rfin = 1;
			
			continue;
		}
	}
		
	/* try to close */
	while (i++ < 3 && ctrl_c == 0) {
		struct timeval tv;
		
		state = 0;
		
		pc->mscb.flags = TH_FIN | TH_ACK | TH_PUSH;
		m = fpacket(pc);
	
		if (m == NULL) {
			printf("out of memory\n");
			return 0;
		}
		
		/* push m into the background output queue which is watched by pth_output */
		enq(&pc->bgoq, m);   
		
		/* expect ACK */
		gettimeofday(&(tv), (void*)0);
		while (!timeout(tv, pc->mscb.waittime) && !ctrl_c) {
			delay_ms(1);
			while ((p = deq(&pc->iq)) != NULL) {
				ok = fresponse(p, &pc->mscb);
				del_pkt(p);
				
				if (!ok)
					continue;
					
				if ((pc->mscb.rflags & (TH_ACK | TH_FIN) ) == (TH_ACK | TH_FIN)) 
					state = 1;
				else if (pc->mscb.rflags == TH_ACK) 
					state = 2;

				tv.tv_sec = 0;
				break;
			}
		}
		
		if (state == 0)
			continue;
		
		/* both side sent and received FIN/ACK, closed! */
		if (rfin == 1 && state == 2)
			return 1;
		
		/* local send FIN/ACK first */
		if (state == 2) {
			struct timeval tv;
			/* expect FIN */
			state = 0;
			//k = 0;
			gettimeofday(&(tv), (void*)0);
			while (!timeout(tv, pc->mscb.waittime) && !ctrl_c) {
				delay_ms(1);
				while ((p = deq(&pc->iq)) != NULL) {	
					ok = fresponse(p, &pc->mscb);
					del_pkt(p);
					if (!ok)
						continue;
						
					if ((pc->mscb.rflags & TH_FIN) == TH_FIN)  {
						/* change my seq, seq += 1 */
						pc->mscb.seq = pc->mscb.rack;
						state = 1;
					}
					tv.tv_sec = 0;
					break;
				}
			}
		}
		
		/* ACK was not received in the time */
		if (state == 0)
			return 0;
		
		/* the remote sent FIN/ACK, response the ACK */
		pc->mscb.ack++;
		tcp_ack(pc, ipv);
		
		return 1;
	}
	return 0;			
}

/* tcp processor
 * return PKT_DROP/PKT_UP
 */
int tcpReplyPacket(tcphdr *th, sesscb *cb, int tcplen)
{
	int clientfinack = 0; /* ack for fin was reply if 1 */
	int dsize = 0;

	th->th_sport ^= th->th_dport;
	th->th_dport ^= th->th_sport;
	th->th_sport ^= th->th_dport;
	
	cb->ack = ntohl(th->th_seq);
	cb->rflags = th->th_flags;
	cb->winsize = ntohs(th->th_win);
	
	if (cb->flags != (TH_RST | TH_FIN)) {		
		switch (th->th_flags) {
			case TH_SYN:
				cb->flags = TH_ACK | TH_SYN;
				cb->ack++;
				break;
			case TH_ACK | TH_PUSH:
				cb->flags = TH_ACK;
				dsize = tcplen - (th->th_off << 2);
				break;
			case TH_ACK | TH_FIN:
				cb->flags = (TH_ACK | TH_FIN);
				cb->ack++;
				break;
			case TH_ACK | TH_FIN | TH_PUSH:
			case TH_FIN | TH_PUSH:
				dsize = tcplen - (th->th_off << 2);

			case TH_FIN:
				if (cb->flags == (TH_ACK | TH_FIN))
					cb->flags = TH_FIN | TH_ACK;
				else
					cb->flags = TH_ACK;
				if (dsize == 0)
					cb->ack++;
				clientfinack = 1;
				break;
			default:
				return 0;	
		}
	}
	if (th->th_flags != TH_SYN)
		cb->seq = ntohl(th->th_ack);
	cb->ack += dsize;
	th->th_ack = htonl(cb->ack);
	th->th_seq = htonl(cb->seq);
	th->th_flags = cb->flags;
	
	/* ignore the tcp options */
	if ((th->th_off << 2) > sizeof(tcphdr))
		th->th_off = sizeof(tcphdr) >> 2;
		
	if (clientfinack == 1)
		return 2;
	
	return 1;
}

int tcp(pcs *pc, struct packet *m)
{
	ethdr *ethernet_header = (ethdr*)(m->data);
	iphdr *ip = (iphdr *)(ethernet_header + 1);
	tcpiphdr *ti = (tcpiphdr *)(ip);
	sesscb *cb = NULL;
	struct packet *p = NULL;
	int i;
	
	if (ip->dip != pc->ip4.ip)
		return PKT_DROP;

	// printf("sock: %d\n", pc->mscb.sock);
	// printf("dport %d %d\n", ntohs(ti->ti_dport), pc->mscb.sport);
	// printf("sport %d %d\n", ntohs(ti->ti_sport), pc->mscb.dport);
	// printf("sip %d %d\n", ip->sip, pc->mscb.dip);
	// printf("proto %d %d\n", ip->proto, pc->mscb.proto);

	/* response packet 
	 * 1. socket opened
	 * 2. same port
	 * 3. destination is me
	 * 4. mscb.proto is TCP
	 */
	if (pc->mscb.sock && ntohs(ti->ti_dport) == pc->mscb.sport && 
	    ntohs(ti->ti_sport) == pc->mscb.dport && 
	    ip->sip == pc->mscb.dip && pc->mscb.proto == ip->proto) {
		/* mscb is actived, up to the upper application */
		if (time_tick - pc->mscb.timeout <= TCP_TIMEOUT)
			return PKT_UP;

		/* not mine, reset the request */
		sesscb rcb;
		
		rcb.seq = random();
		rcb.sip = ip->sip;
		rcb.dip = ip->dip;
		rcb.sport = ti->ti_sport;
		rcb.dport = ti->ti_dport;
		rcb.flags = TH_RST | TH_FIN | TH_ACK;
		
		p = tcpReply(m, &rcb);
		
		/* push m into the background output queue which is watched by pth_output */
		if (p != NULL) {
			enq(&pc->bgoq, p);			
		} else
			printf("reply error\n");
		
		/* drop the request packet */
		return PKT_DROP;
	}

	/* request process
	 * find control block 
	 */
	for (i = 0; i < MAX_SESSIONS; i++) {
		if (ti->ti_flags == TH_SYN) {
			if (pc->sesscb[i].timeout == 0 || 
			    time_tick - pc->sesscb[i].timeout > TCP_TIMEOUT ||
			    (ip->sip == pc->sesscb[i].sip && 
			     ip->dip == pc->sesscb[i].dip &&
			     ti->ti_sport == pc->sesscb[i].sport &&
			     ti->ti_dport == pc->sesscb[i].dport)) {
				/* get new scb */
				cb = &pc->sesscb[i];
				cb->timeout = time_tick;
				cb->seq = random();
				cb->sip = ip->sip;
				cb->dip = ip->dip;
				cb->sport = ti->ti_sport;
				cb->dport = ti->ti_dport;
				cb->proto = ip->proto;
				bcopy(ethernet_header->src, cb->dmac, ETH_ALEN);
				bcopy(ethernet_header->dst, cb->smac, ETH_ALEN);
				

				// accept TCP connection as main
				if(ntohs(cb->dport) == pc->tcp_listen_port){
					pc->mscb = *cb;
					u_int temp;

					temp = pc->mscb.dport;
					pc->mscb.dport = ntohs(pc->mscb.sport);
					pc->mscb.sport = ntohs(temp);

					temp = pc->mscb.sip;
					pc->mscb.sip = pc->mscb.dip;
					pc->mscb.dip = temp;

					pc->mscb.sock = 1;
					
					pc->tcp_listen_port = 0;
				}
				
				break;
			}
		} else {
			if ((time_tick - 
			    pc->sesscb[i].timeout <= TCP_TIMEOUT) && 
			    ip->sip == pc->sesscb[i].sip && 
			    ip->dip == pc->sesscb[i].dip &&
			    ti->ti_sport == pc->sesscb[i].sport &&
			    ti->ti_dport == pc->sesscb[i].dport) {
				/* get the scb */
				cb = &pc->sesscb[i];
				break;
			}	
		}
	}
	
	if (ti->ti_flags == TH_SYN && cb == NULL) {
		printf("VPCS %d out of session\n", pc->id);
		return PKT_DROP;
	}
	
	if (cb != NULL) {
		if (ti->ti_flags == TH_ACK && cb->flags == TH_FIN) {
			/* clear session */
			memset(cb, 0, sizeof(sesscb));
		} else {
			cb->timeout = time_tick;
			p = tcpReply(m, cb);
			
			/* push m into the background output queue which is watched by pth_output */
			if (p != NULL) {
				enq(&pc->bgoq, p);
			}
			
			/* send FIN after ACK if got FIN */
			if ((cb->rflags & TH_FIN) == TH_FIN && 
			    cb->flags == (TH_ACK | TH_FIN)) {
				p = tcpReply(m, cb);
				
				/* push m into the background output queue which is watched by pth_output */
				if (p != NULL) {
					enq(&pc->bgoq, p);
				}
			}
		}
	}

	/* anyway tell caller to drop this packet */
	return PKT_DROP;	
}

struct packet *tcpReply(struct packet *m0, sesscb *cb)
{
	struct packet *m;

	m = new_pkt(sizeof(ethdr) + sizeof(iphdr) + sizeof(tcphdr));
	if (m == NULL)
		return NULL;
	
	memcpy(m->data, m0->data, m->len);

	ethdr *eh = (ethdr *)(m->data);
	iphdr *ip = (iphdr *)(eh + 1);
	tcpiphdr *ti = (tcpiphdr *)ip;
	tcphdr *th = (tcphdr *)(ip + 1);
	char *end_of_message = (char*)(m->data) + m->len;
	
	int received_tcp_length = ntohs(ip->len) - sizeof(iphdr);

	ip->len = htons(end_of_message - (char*)ip);
	ip->dip ^= ip->sip;
	ip->sip ^= ip->dip;
	ip->dip ^= ip->sip;
	
	int rt = tcpReplyPacket(th, cb, received_tcp_length);
	if (rt == 0) {
		del_pkt(m);
		return NULL;
	} 
	
	// TCP pseudo header for purposes of TCP checksum calculation
	char *tcp_pseudo_header = ((char*)ti) + 8;
	ip->ttl = 0;
	ip->cksum = sizeof(tcpiphdr);
	ti->ti_len = htons(sizeof(tcphdr));
	
	ti->ti_sum = 0;
	ti->ti_sum = cksum((u_short*)tcp_pseudo_header, end_of_message - tcp_pseudo_header);

	// restore values in IP header
	ip->ttl = TTL;

	// calculate checksum of IP header
	ip->cksum = 0;
	ip->cksum = cksum((u_short *)ip, sizeof(iphdr));
	
	swap_ehead(m->data);
	
	/* save the status, ACK for TH_FIN of client was sent 
	 * so send FIN on the next time
	 */
	if (rt == 2)
		cb->flags = (TH_ACK | TH_FIN);
		
	return m;	
}

int tcp6(pcs *pc, struct packet *m)
{
	ip6hdr *ip = (ip6hdr *)(m->data + sizeof(ethdr));
	struct tcphdr *th = (struct tcphdr *)(ip + 1);
	sesscb *cb = NULL;
	struct packet *p = NULL;
	int i;

	/* from linklocal */
	if (ip->src.addr16[0] == IPV6_ADDR_INT16_ULL) {
		if (!IP6EQ(&(pc->link6.ip), &(ip->dst)))// || th->th_sport != th->th_dport)
			return PKT_DROP;
	} else {
		if (!IP6EQ(&(pc->ip6.ip), &(ip->dst)))// || th->th_sport != th->th_dport)
			return PKT_DROP;
	}

	/* response packet 
	 * 1. socket opened
	 * 2. same port
	 * 3. destination is me
	 * 4. mscb.proto is TCP
	 */
	if (pc->mscb.sock && pc->mscb.proto == ip->ip6_nxt &&
	    ntohs(th->th_dport) == pc->mscb.sport && 
	    ntohs(th->th_sport) == pc->mscb.dport &&
	    IP6EQ(&(pc->mscb.dip6), &(ip->src))) {
		/* mscb is actived, up to the upper application */
		if (time_tick - pc->mscb.timeout <= TCP_TIMEOUT)
			return PKT_UP;

		/* not mine, reset the request*/
		sesscb rcb;
		
		rcb.seq = random();
		memcpy(rcb.sip6.addr8, ip->src.addr8, 16);
		memcpy(rcb.dip6.addr8, ip->dst.addr8, 16);
		rcb.sport = th->th_sport;
		rcb.dport = th->th_dport;
		rcb.flags = TH_RST | TH_FIN | TH_ACK;
		rcb.seq = time(0);
		
		p = tcp6Reply(m, &rcb);
		
		/* push m into the background output queue which is watched by pth_output */
		if (p != NULL) {
			enq(&pc->bgoq, p);
		} else
			printf("reply error\n");
		
		/* drop the request packet */
		return PKT_DROP;
	}

	/* request process
	 * find control block 
	 */
	for (i = 0; i < MAX_SESSIONS; i++) {
		if (th->th_flags == TH_SYN) {
			if (pc->sesscb[i].timeout == 0 || 
				(IP6EQ(&(pc->sesscb[i].sip6), &(ip->src)) && 
				 IP6EQ(&(pc->sesscb[i].dip6), &(ip->dst)) &&
				 th->th_sport == pc->sesscb[i].sport &&
				 th->th_dport == pc->sesscb[i].dport)) {
				/* get new scb */
				cb = &pc->sesscb[i];
				cb->timeout = time_tick;
				cb->seq = random();
				memcpy(cb->sip6.addr8, ip->src.addr8, 16);
				memcpy(cb->dip6.addr8, ip->dst.addr8, 16);
				cb->sport = th->th_sport;
				cb->dport = th->th_dport;

				break;
			}
		} else {

	
			if ((time_tick - 
			    pc->sesscb[i].timeout <= TCP_TIMEOUT) && 
			    IP6EQ(&(pc->sesscb[i].sip6), &(ip->src)) && 
			    IP6EQ(&(pc->sesscb[i].dip6), &(ip->dst)) &&
			    th->th_sport == pc->sesscb[i].sport &&
			    th->th_dport == pc->sesscb[i].dport) {
				/* get the scb */
				cb = &pc->sesscb[i];
				break;
			}	
		}
	}

	if (th->th_flags == TH_SYN && cb == NULL) {
		printf("VPCS %d out of session\n", pc->id);
		return PKT_DROP;
	}

	if (cb != NULL) {
		if (th->th_flags == TH_ACK && cb->flags == TH_FIN) {
			/* clear session */
			memset(cb, 0, sizeof(sesscb));
		} else {
			cb->timeout = time_tick;
			p = tcp6Reply(m, cb);
			
			/* push m into the background output queue which is watched by pth_output */
			if (p != NULL) {
				enq(&pc->bgoq, p);
			}

			/* send FIN after ACK if got FIN */	
			if ((cb->rflags & TH_FIN) == TH_FIN && cb->flags == (TH_ACK | TH_FIN)) {	
				p = tcp6Reply(m, cb);
				/* push m into the background output queue which is watched by pth_output */
				if (p != NULL) {
					enq(&pc->bgoq, p);
				}
			}
		}
	}

	/* anyway tell caller to drop this packet */
	return PKT_DROP;	
}

struct packet *tcp6Reply(struct packet *m0, sesscb *cb)
{
	ethdr *eh;
	ip6hdr *ip;
	tcphdr *th;
	struct packet *m;
	int len;
	int tcplen = 0;
	
	len = sizeof(ethdr) + sizeof(ip6hdr) + sizeof(tcphdr);
	m = new_pkt(len);
	if (m == NULL)
		return NULL;
		
	memcpy(m->data, m0->data, m->len);
	
	eh = (ethdr *)(m->data);
	ip = (ip6hdr *)(eh + 1);
	th = (struct tcphdr *)(ip + 1);
		
	swap_ehead(m->data);
	swap_ip6head(m);

	ip->ip6_hlim = TTL;
	tcplen = ntohs(ip->ip6_plen);
	ip->ip6_plen = htons((u_short)sizeof(tcphdr));
	
	int rt = tcpReplyPacket(th, cb, tcplen);
	if (rt == 0) {
		del_pkt(m);
		return NULL;
	} 

	th->th_sum = 0;
	th->th_sum = cksum6(ip, IPPROTO_TCP, len);
	
	/* save the status, ACK for TH_FIN of client was sent 
	 * so send FIN on the next time
	 */
	if (rt == 2)
		cb->flags = (TH_ACK | TH_FIN);
		
	return m;	
}

char *tcp_get_data(struct packet *m){
	ethdr *eh = (ethdr *)(m->data);
	iphdr *ip = (iphdr *)(eh + 1);
	tcphdr *th = (tcphdr *)(ip + 1);

	return (char*)(th) + 4*th->th_off;
}

int tcp_get_length(struct packet *m){
	return m->data + m->len - tcp_get_data(m);
}

struct packet *tcp_prepare_packet(sesscb *cb, const char* data, int len){
	struct packet* p = new_pkt(sizeof(ethdr) + sizeof(tcpiphdr) + len);

	ethdr *eh = (ethdr *)(p->data);
	iphdr *ip = (iphdr *)(eh + 1);
	tcpiphdr *ti = (tcpiphdr *)ip;
	tcphdr *th = (tcphdr *)(ip + 1);
	char *payload = (char*)(th + 1);
	char *end_of_message = (char*)(p->data) + p->len;


	// payload
	bcopy(data, payload, len);

	// tcp
	th->th_sport = htons(cb->sport);
	th->th_dport = htons(cb->dport);
	th->th_seq = htonl(cb->seq);
	th->th_ack = htonl(cb->ack);
	cb->flags = TH_PUSH | TH_ACK;
	th->th_flags = cb->flags;
	th->th_off = sizeof(tcphdr) >> 2;
	th->th_win = htons(1000);


	cb->seq += len;

	// ip
	ip->ver = 4;
	ip->ihl = sizeof *ip >> 2;
	ip->tos = 0x10;
	ip->id = 0;
	ip->ttl = TTL;
	ip->proto = cb->proto;
	ip->dip = cb->dip;
	ip->sip = cb->sip;
	ip->len = htons(end_of_message - (char*)ip);
	ip->proto = IPPROTO_TCP;



	// TCP pseudo header for purposes of TCP checksum calculation
	char *tcp_pseudo_header = ((char*)ti) + 8;
	ip->ttl = 0;
	ip->cksum = sizeof(tcpiphdr);
	ti->ti_len = htons(end_of_message - (char*)th);
	
	ti->ti_sum = 0;
	ti->ti_sum = cksum((u_short*)tcp_pseudo_header, end_of_message - tcp_pseudo_header);

	// restore values in IP header
	ip->ttl = TTL;

	// calculate checksum of IP header
	ip->cksum = 0;
	ip->cksum = cksum((u_short *)ip, sizeof(iphdr));

	// ethernet
	encap_ehead(p->data, cb->smac, cb->dmac, ETHERTYPE_IP);

	return p;
}

sesscb *tcp_accept(pcs *pc, int port){
	pc->tcp_listen_port = port;
	while(pc->tcp_listen_port);

	return &pc->mscb;
}

/* end of file */
