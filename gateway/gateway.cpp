/*
 * gateway.cpp
 *
 *  Created on: 2014. 1. 10.
 *      Author: leeopop
 */


#include <iostream>

#include <set>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdbool>
#include <vector>
#include <unordered_set>
#include <map>
#include <thread>


extern "C"
{
#include <signal.h>
#include <pthread.h>


#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_tailq.h>
#include <rte_prefetch.h>
#include <rte_pci.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_timer.h>

}

#include "util.hh"

#define DEFAULT_SOCK 0
#define MTU 1518
#define NUM_MBUF	(4096-1)
#define MBUF_SIZE	(MTU + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)

using namespace std;

static void init_dpdk(void)
{
	rte_timer_subsystem_init();
	rte_set_log_level(RTE_LOG_DEBUG);
	rte_set_log_type(RTE_LOGTYPE_PMD, false);
	rte_set_log_type(RTE_LOGTYPE_MALLOC, false);
	rte_set_log_type(RTE_LOGTYPE_MEMPOOL, true);


	if (rte_pmd_init_all() < 0)
		rte_exit(EXIT_FAILURE, "cannot initialize poll-mode devices.\n");

	if (rte_eal_pci_probe() < 0)
		rte_exit(EXIT_FAILURE, "cannot probe PCI bus.\n");

	unsigned num_ports = rte_eth_dev_count();
	if (num_ports == 0)
		rte_exit(EXIT_FAILURE, "no available/compatible ehternet ports.\n");

}

static void init_nic(const string &inside_nic_mac, const string &outside_nic_mac, int* inside_port_idx, int* outside_port_idx)
{
	*inside_port_idx = *outside_port_idx = -1;
	char inside_mac[6];
	char outside_mac[6];
	if(!string_to_mac(inside_nic_mac, inside_mac))
		rte_exit(EXIT_FAILURE, "Cannot parse inside mac.\n");
	if(!string_to_mac(outside_nic_mac, outside_mac))
		rte_exit(EXIT_FAILURE, "Cannot parse outside mac.\n");
	if(memcmp(inside_mac, outside_mac, 6) == 0)
		rte_exit(EXIT_FAILURE, "in and out mac are same.\n");

	struct rte_eth_conf port_conf;
	memset(&port_conf, 0, sizeof(port_conf));
	port_conf.rxmode.mq_mode		= ETH_MQ_RX_NONE;
	port_conf.rxmode.max_rx_pkt_len = MTU; /* only used if jumbo_frame is enabled */
	port_conf.rxmode.split_hdr_size = 0;
	port_conf.rxmode.header_split	= false;
	port_conf.rxmode.hw_ip_checksum = false;
	port_conf.rxmode.hw_vlan_filter = true;
	port_conf.rxmode.hw_vlan_strip	= true;
	port_conf.rxmode.hw_vlan_extend = false;
	port_conf.rxmode.jumbo_frame	= false;
	port_conf.rxmode.hw_strip_crc	= false;
	port_conf.txmode.mq_mode	= ETH_MQ_TX_NONE;
	// TODO: set-up flow director table by reading configuration on which layers and elements are used!
	port_conf.fdir_conf.mode	= RTE_FDIR_MODE_NONE;
	port_conf.fdir_conf.pballoc = RTE_FDIR_PBALLOC_64K;
	port_conf.fdir_conf.status	= RTE_FDIR_NO_REPORT_STATUS;
	port_conf.fdir_conf.flexbytes_offset = 0;
	port_conf.fdir_conf.drop_queue		 = -1;  // TODO: assign an extra queue to get non-matched packets

	/* Per RX-queue configuration */
	unsigned num_ports = rte_eth_dev_count();
	struct rte_eth_rxconf rx_conf;
	memset(&rx_conf, 0, sizeof(rx_conf));
	rx_conf.rx_thresh.pthresh = 8;
	rx_conf.rx_thresh.hthresh = 8;
	rx_conf.rx_thresh.wthresh = 4;
	rx_conf.rx_free_thresh = 0;
	rx_conf.rx_drop_en	   = 0; /* when enabled, drop packets if no descriptors are available */

	/* Per TX-queue configuration */
	struct rte_eth_txconf tx_conf;
	memset(&tx_conf, 0, sizeof(tx_conf));
	tx_conf.tx_thresh.pthresh = 36;
	tx_conf.tx_thresh.hthresh = 0;
	tx_conf.tx_thresh.wthresh = 0;
	/* The following rs_thresh and flag value enables "simple TX" function. */
	tx_conf.tx_rs_thresh   = 0;
	tx_conf.tx_free_thresh = 0; /* use PMD default value */
	tx_conf.txq_flags	   = 0;

	unsigned char port_idx;
	for (port_idx = 0; port_idx < num_ports; port_idx++) {
		char dev_addr_buf[64];
		struct ether_addr macaddr;
		struct rte_eth_dev_info dev_info;

		rte_eth_dev_info_get((uint8_t) port_idx, &dev_info);

		sprintf(dev_addr_buf, "%04x:%02x:%02x.%u", dev_info.pci_dev->addr.domain,
				dev_info.pci_dev->addr.bus,
				dev_info.pci_dev->addr.devid,
				dev_info.pci_dev->addr.function);

		/* Check the available RX/TX queues. */
		if (1 > dev_info.max_rx_queues)
			rte_exit(EXIT_FAILURE, "port (%u, %s) does not support request number of rxq.\n",
					port_idx, dev_info.driver_name);
		if (1 > dev_info.max_tx_queues)
			rte_exit(EXIT_FAILURE, "port (%u, %s) does not support request number of txq.\n",
					port_idx, dev_info.driver_name);

		printf("gateway: port %d's queue stataus. (%d, %d)\n", dev_info.max_rx_queues, dev_info.max_tx_queues);

		rte_eth_macaddr_get((uint8_t) port_idx, &macaddr);
		if(memcmp(macaddr.addr_bytes, inside_mac, 6) == 0)
		{
			*inside_port_idx = port_idx;
			printf("gateway: inside port %u: %s, bus location %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
					port_idx, dev_info.driver_name,
					dev_addr_buf,
					macaddr.addr_bytes[0], macaddr.addr_bytes[1], macaddr.addr_bytes[2],
					macaddr.addr_bytes[3], macaddr.addr_bytes[4], macaddr.addr_bytes[5]);
		}
		else if(memcmp(macaddr.addr_bytes, outside_mac, 6) == 0)
		{
			*outside_port_idx = port_idx;
			printf("gateway: outside port %u: %s, bus location %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
					port_idx, dev_info.driver_name,
					dev_addr_buf,
					macaddr.addr_bytes[0], macaddr.addr_bytes[1], macaddr.addr_bytes[2],
					macaddr.addr_bytes[3], macaddr.addr_bytes[4], macaddr.addr_bytes[5]);
		}
		else
		{
			printf("gateway: unused port %u: %s, bus location %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
					port_idx, dev_info.driver_name,
					dev_addr_buf,
					macaddr.addr_bytes[0], macaddr.addr_bytes[1], macaddr.addr_bytes[2],
					macaddr.addr_bytes[3], macaddr.addr_bytes[4], macaddr.addr_bytes[5]);
			continue;
		}

		rte_eth_dev_configure((uint8_t) port_idx, 1, 1, &port_conf);


		int ret = rte_eth_tx_queue_setup((uint8_t) port_idx, 0, 512, DEFAULT_SOCK, &tx_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, port=%d, qidx=%d\n",
					ret, port_idx, 0);


		{
			char temp_buf[RTE_MEMPOOL_NAMESIZE];
			snprintf(temp_buf, RTE_MEMPOOL_NAMESIZE,
					"pktbuf_n%u_d%u_r%u", 0, port_idx, 0);
			struct rte_mempool *mp;
			mp = rte_mempool_create(temp_buf, NUM_MBUF, MBUF_SIZE, 64,
					sizeof(rte_pktmbuf_pool_private),
					rte_pktmbuf_pool_init, NULL,
					rte_pktmbuf_init, NULL,
					DEFAULT_SOCK, MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET);
			if (mp == NULL)
				rte_exit(EXIT_FAILURE, "cannot allocate memory pool for rxq %u:%u@%u.\n",
						port_idx, 0, DEFAULT_SOCK);
			ret = rte_eth_rx_queue_setup((uint8_t) port_idx, 0, 128, DEFAULT_SOCK, &rx_conf,
					mp);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup: err=%d, port=%d, qidx=%d\n",
						ret, port_idx, 0);
		}

		/* Start RX/TX processing in the NIC! */


		rte_eth_allmulticast_enable((uint8_t) port_idx);
		rte_eth_promiscuous_enable((uint8_t) port_idx);

		ret = rte_eth_dev_start((uint8_t) port_idx);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start");


		printf("gateway: port %d is enabled. (%d)\n", port_idx, rte_eth_promiscuous_get(port_idx));
	}
}



static bool loop_ctrl = true;

static void exit_handle(int num)
{
	loop_ctrl = false;
	printf("Exiting...\n");
	fflush(0);
}


static void serve(int inside, int outside, bool * loop_ctrl)
{
	struct rte_mbuf *inside_rx_pkt[32];
	struct rte_mbuf *inside_tx_pkt[32];
	struct rte_mbuf *outside_rx_pkt[32];
	struct rte_mbuf *outside_tx_pkt[32];

	int prev1=0;
	int prev2=0;

	while(*loop_ctrl)
	{
		int in_rx_cnt = rte_eth_rx_burst((uint8_t)inside, 0, inside_rx_pkt, 32);
		int out_rx_cnt = rte_eth_rx_burst((uint8_t)outside, 0, outside_rx_pkt, 32);

		int inside_tx_cnt = 0;
		int outside_tx_cnt = 0;


		for(int k=0; k<in_rx_cnt; k++)
		{
			outside_tx_pkt[outside_tx_cnt++] = inside_rx_pkt[k];
		}


		for(int k=0; k<out_rx_cnt; k++)
		{
			inside_tx_pkt[inside_tx_cnt++] = outside_rx_pkt[k];
		}

		struct rte_eth_stats stats1, stats2;
		rte_eth_stats_get(0, &stats1);
		rte_eth_stats_get(1, &stats2);

		if(prev1 != stats1.ipackets || prev2 != stats2.ipackets)
		{
			prev1 = stats1.ipackets;
			prev2 = stats2.ipackets;
			printf("%d %d\n", prev1, prev2);
		}

		if(in_rx_cnt == 0 && out_rx_cnt == 0)
		{
			pthread_yield();
			continue;
		}

		printf("inside %d, outside %d has received.\n", in_rx_cnt, out_rx_cnt);

		int inside_tx_sent = 0;
		while(inside_tx_sent < inside_tx_cnt)
		{
			inside_tx_sent += rte_eth_tx_burst(inside, 0, inside_tx_pkt + inside_tx_sent, inside_tx_cnt - inside_tx_sent);
		}

		int outside_tx_sent = 0;
		while(outside_tx_sent < outside_tx_cnt)
		{
			outside_tx_sent += rte_eth_tx_burst(outside, 0, outside_tx_pkt + outside_tx_sent, outside_tx_cnt - outside_tx_sent);
		}

		printf("inside %d, outside %d has sent.\n", inside_tx_sent, outside_tx_sent);


	}
}

int main(int argc, char** argv)
{
	signal(SIGINT, exit_handle);

	int ret = rte_eal_init(argc, argv);
	argc -= ret;
	argv += ret;

	init_dpdk();


	string temp_inside("00:15:17:91:1F:0B");
	string temp_outside("00:15:17:9F:E0:1C");
	int inside, outside;
	init_nic(temp_inside, temp_outside, &inside, &outside);

	if(inside == -1)
		rte_exit(EXIT_FAILURE, "Cannot find inside dev.\n");
	if(outside == -1)
		rte_exit(EXIT_FAILURE, "Cannot find outside dev.\n");

	serve(inside, outside, &loop_ctrl);


	printf("Exit.\n");

	return 0;
}
