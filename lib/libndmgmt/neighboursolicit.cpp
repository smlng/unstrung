/*
 * Copyright (C) 2016 Michael Richardson <mcr@sandelman.ca>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <errno.h>
#include <pathnames.h>		/* for PATH_PROC_NET_IF_INET6 */
#include <time.h>
#include <arpa/inet.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <linux/if.h>           /* for IFNAMSIZ */
#include "oswlibs.h"
#include "rpl.h"
#include "neighbour.h"
}

#include "iface.h"
#include "devid.h"

rpl_node & network_interface::find_neighbour(struct in6_addr nip)
{
    rpl_node &target = this->neighbours[nip];
    if(!target.validP()) {
        target.markvalid(this->get_if_index(), nip, this->debug);
    }
    return target;
}


void network_interface::receive_neighbour_solicit(struct in6_addr from,
                                                    struct in6_addr ip6_to,
                                                    const  time_t now,
                                                    const u_char *dat, const int nd_len)
{
    unsigned int dat_len = nd_len;
    bool newNode = false;
    struct nd_neighbor_solicit *ns = (struct nd_neighbor_solicit *)dat;
    debug->info("  processing NS(%u)\n",nd_len);

    dag_network::globalStats[PS_NEIGHBOUR_SOLICIT]++;

    if(this->packet_too_short("ns", nd_len, sizeof(*ns))) return;

    /* there are a number of reasons to see an NS message */

    /* look for the target node, and find the related node */
    struct in6_addr *neighbourv6 = &ns->nd_ns_target;

    rpl_node &target = find_neighbour(*neighbourv6);
    newNode = !target.validP();

    /* 1. if the destination address is our address, then the node is trying to confirm that
     *    we exist, and we should respond with a straight NA.
     *    RFC4861, section 4.3:
     *          Neighbor Solicitations are multicast when the node needs
     *          to resolve an address and UNICAST WHEN THE NODE SEEKS TO VERIFY THE
     *          reachability of a neighbor.
     */
    if(this->matching_address(ip6_to)) {
        if(this->matching_address(*neighbourv6)) {
            dag_network::globalStats[PS_NEIGHBOUR_UNICAST_TARGET_NS]++;
        }
	dag_network::globalStats[PS_NEIGHBOUR_UNICAST_REACHABILITY]++;
        reply_neighbour_advert(from, ip6_to, now, ns, nd_len);
        return;
    }

    /* 2. if the destination address is rather a  multicast,
     *    it's probably for the multicast group (the solicited node) multicast
     *    that includes the address it is looking for.
     *
     */
    if(IN6_IS_ADDR_MULTICAST(ip6_to.s6_addr)) {
        target.markvalid(this->get_if_index(), *neighbourv6, this->debug);

        if(this->all_hosts_addrP(ip6_to)) {
            dag_network::globalStats[PS_NEIGHBOUR_JOINASSISTANT_SOLICIT]++;
            target.reply_mcast_neighbour_join(this, from, ip6_to, now, ns, nd_len);
        }  else {
            /* should check here for solicited node multicast group */
            /* ip6_to.s6_addr[13] == 0xff and ip6_to.s6_addr[12] == 0xff */
            dag_network::globalStats[PS_NEIGHBOUR_MCAST_SOLICIT]++;
            target.reply_mcast_neighbour_advert(this, from, ip6_to, now, ns, nd_len);
        }
        return;
    }

    dag_network::globalStats[PS_NEIGHBOUR_IGNORED]++;

}

/*
 * a NS will be sent as part of the join process to let other devices
 * know that it exists.
 * It will cause a DAR/DAC process to be initiated upwards.
 *
 */
int device_identity::build_neighbour_solicit(network_interface *iface,
                                            unsigned char *buff,
                                            unsigned int buff_len)
{
    struct sockaddr_in6 addr;
    struct in6_addr *dest = NULL;
    struct icmp6_hdr  *icmp6;
    struct nd_neighbor_advert *nna;
    unsigned char *nextopt;
    int optlen;
    int len = 0;

    memset(buff, 0, buff_len);

    icmp6 = (struct icmp6_hdr *)buff;
    icmp6->icmp6_type = ND_NEIGHBOR_SOLICIT;
    icmp6->icmp6_code = 0;
    icmp6->icmp6_cksum = 0;

    nna = (struct nd_neighbor_advert *)icmp6;
    nextopt = (unsigned char *)(nna+1);

    /*
     * ND_NA_FLAG_ROUTER    is off.
     * ND_NA_FLAG_SOLICITED is off.
     */
    nna->nd_na_flags_reserved = ND_NA_FLAG_OVERRIDE;
    nna->nd_na_target         = iface->link_local();

    /*
     * now add ARO option in
     */
    struct nd_opt_aro *noa = (struct nd_opt_aro *)nextopt;
    nextopt = (unsigned char *)(noa+1);

    noa->nd_aro_type = ND_OPT_ARO;
    noa->nd_aro_len  = (sizeof(struct nd_opt_aro)-8)/4; /* 32-bit units */
    noa->nd_aro_status   = 0;
    noa->nd_aro_lifetime = htons(ND_ARO_DEFAULT_LIFETIME);
    memcpy(noa->nd_aro_eui64, iface->get_eui64(), 8);

    /* recalculate length */
    len = ((caddr_t)nextopt - (caddr_t)buff);

    len = (len + 7)&(~0x7);  /* round up to next multiple of 64-bits */
    return len;
}


void network_interface::send_ns(device_identity &di)
{
    unsigned char icmp_body[2048];

    debug->log("sending Neighbour Solication on if: %s%s\n",
               this->if_name,
	       this->faked() ? "(faked)" : "");
    memset(icmp_body, 0, sizeof(icmp_body));

    unsigned int icmp_len = di.build_neighbour_solicit(this, icmp_body, sizeof(icmp_body));

    struct in6_addr all_hosts_inaddr;
    struct in6_addr unspecified_src;
    memcpy(all_hosts_inaddr.s6_addr, all_hosts_addr, 16);
    memset(unspecified_src.s6_addr, 0, 16);
    send_raw_icmp(&all_hosts_inaddr, &unspecified_src, icmp_body, icmp_len);
}

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: whitesmith
 * compile-command: "make programs"
 * End:
 */
