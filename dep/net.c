/**
 * @file   net.c
 * @date   Tue Jul 20 16:17:49 2010
 * 
 * @brief  Functions to interact with the network sockets and NIC driver.
 * 
 * 
 */

#include "../ptpd.h"
#include <netdb.h>

/* shut down the UDP stuff */
Boolean 
netShutdown(NetPath * netPath)
{
	struct ip_mreq imr;

	/**
	 * �˳��ಥ��
	 */
	/* Close General Multicast */
	imr.imr_multiaddr.s_addr = netPath->multicastAddr;
	imr.imr_interface.s_addr = htonl(INADDR_ANY);

	setsockopt(netPath->eventSock, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
		   &imr, sizeof(struct ip_mreq));
	setsockopt(netPath->generalSock, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
		   &imr, sizeof(struct ip_mreq));

	/* Close Peer Multicast */
	imr.imr_multiaddr.s_addr = netPath->peerMulticastAddr;
	imr.imr_interface.s_addr = htonl(INADDR_ANY);

	setsockopt(netPath->eventSock, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
		   &imr, sizeof(struct ip_mreq));
	setsockopt(netPath->generalSock, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
		   &imr, sizeof(struct ip_mreq));
	/**
	 * �˳��ಥ��
	 */


	netPath->multicastAddr = 0;
	netPath->unicastAddr = 0;
	netPath->peerMulticastAddr = 0;

	/* Close sockets */
	if (netPath->eventSock > 0)
		close(netPath->eventSock);
	netPath->eventSock = -1;

	if (netPath->generalSock > 0)
		close(netPath->generalSock);
	netPath->generalSock = -1;

	return TRUE;
}

/*Test if network layer is OK for PTP*/
UInteger8 
lookupCommunicationTechnology(UInteger8 communicationTechnology)
{
#if defined(linux)

	switch (communicationTechnology) {
	case ARPHRD_ETHER:
	case ARPHRD_EETHER:
	case ARPHRD_IEEE802:
		return PTP_ETHER;

	default:
		break;
	}

#elif defined(BSD_INTERFACE_FUNCTIONS)

#endif

	return PTP_DEFAULT;
}


 /* Find the local network interface */
UInteger32 
findIface(Octet * ifaceName, UInteger8 * communicationTechnology,
    Octet * uuid, NetPath * netPath)
{
#if defined(linux)

	/* depends on linux specific ioctls (see 'netdevice' man page) */
	int i, flags;
	struct ifconf data;
	struct ifreq device[IFCONF_LENGTH];

	data.ifc_len = sizeof(device);
	data.ifc_req = device;

	memset(data.ifc_buf, 0, data.ifc_len);

	flags = IFF_UP | IFF_RUNNING | IFF_MULTICAST;

	/* look for an interface if none specified */
	if (ifaceName[0] != '\0') {
		i = 0;
		memcpy(device[i].ifr_name, ifaceName, IFACE_NAME_LENGTH);

		if (ioctl(netPath->eventSock, SIOCGIFHWADDR, &device[i]) < 0)
			DBGV("failed to get hardware address\n");
		else if ((*communicationTechnology = 
			  lookupCommunicationTechnology(
				  device[i].ifr_hwaddr.sa_family)) 
			 == PTP_DEFAULT)
			DBGV("unsupported communication technology (%d)\n", 
			     *communicationTechnology);
		else
			memcpy(uuid, device[i].ifr_hwaddr.sa_data, 
			       PTP_UUID_LENGTH);
	} else {
		/* no iface specified */
		/* get list of network interfaces */
		if (ioctl(netPath->eventSock, SIOCGIFCONF, &data) < 0) {
			PERROR("failed query network interfaces");
			return 0;
		}
		if (data.ifc_len >= sizeof(device))
			DBG("device list may exceed allocated space\n");

		/* search through interfaces */
		for (i = 0; i < data.ifc_len / sizeof(device[0]); ++i) {
			DBGV("%d %s %s\n", i, device[i].ifr_name, 
			     inet_ntoa(((struct sockaddr_in *)
					&device[i].ifr_addr)->sin_addr));

			if (ioctl(netPath->eventSock, SIOCGIFFLAGS, 
				  &device[i]) < 0)
				DBGV("failed to get device flags\n");
			else if ((device[i].ifr_flags & flags) != flags)
				DBGV("does not meet requirements"
				     "(%08x, %08x)\n", device[i].ifr_flags, 
				     flags);
			else if (ioctl(netPath->eventSock, SIOCGIFHWADDR, 
				       &device[i]) < 0)
				DBGV("failed to get hardware address\n");
			else if ((*communicationTechnology = 
				  lookupCommunicationTechnology(
					  device[i].ifr_hwaddr.sa_family)) 
				 == PTP_DEFAULT)
				DBGV("unsupported communication technology"
				     "(%d)\n", *communicationTechnology);
			else {
				DBGV("found interface (%s)\n", 
				     device[i].ifr_name);
				memcpy(uuid, device[i].ifr_hwaddr.sa_data, 
				       PTP_UUID_LENGTH);
				memcpy(ifaceName, device[i].ifr_name, 
				       IFACE_NAME_LENGTH);
				break;
			}
		}
	}

	if (ifaceName[0] == '\0') {
		ERROR("failed to find a usable interface\n");
		return 0;
	}
	if (ioctl(netPath->eventSock, SIOCGIFADDR, &device[i]) < 0) {
		PERROR("failed to get ip address");
		return 0;
	}
	return ((struct sockaddr_in *)&device[i].ifr_addr)->sin_addr.s_addr;

#elif defined(BSD_INTERFACE_FUNCTIONS)

	struct ifaddrs *if_list, *ifv4, *ifh;

	if (getifaddrs(&if_list) < 0) {
		PERROR("getifaddrs() failed");
		return FALSE;
	}
	/* find an IPv4, multicast, UP interface, right name(if supplied) */
	for (ifv4 = if_list; ifv4 != NULL; ifv4 = ifv4->ifa_next) {
		if ((ifv4->ifa_flags & IFF_UP) == 0)
			continue;
		if ((ifv4->ifa_flags & IFF_RUNNING) == 0)
			continue;
		if ((ifv4->ifa_flags & IFF_LOOPBACK))
			continue;
		if ((ifv4->ifa_flags & IFF_MULTICAST) == 0)
			continue;
                /* must have IPv4 address */
		if (ifv4->ifa_addr->sa_family != AF_INET)
			continue;
		if (ifaceName[0] && strncmp(ifv4->ifa_name, ifaceName, 
					    IF_NAMESIZE) != 0)
			continue;
		break;
	}

	if (ifv4 == NULL) {
		if (ifaceName[0]) {
			ERROR("interface \"%s\" does not exist,"
			      "or is not appropriate\n", ifaceName);
			return FALSE;
		}
		ERROR("no suitable interfaces found!");
		return FALSE;
	}
	/* find the AF_LINK info associated with the chosen interface */
	for (ifh = if_list; ifh != NULL; ifh = ifh->ifa_next) {
		if (ifh->ifa_addr->sa_family != AF_LINK)
			continue;
		if (strncmp(ifv4->ifa_name, ifh->ifa_name, IF_NAMESIZE) == 0)
			break;
	}

	if (ifh == NULL) {
		ERROR("could not get hardware address for interface \"%s\"\n", 
		      ifv4->ifa_name);
		return FALSE;
	}
	/* check that the interface TYPE is OK */
	if (((struct sockaddr_dl *)ifh->ifa_addr)->sdl_type != IFT_ETHER) {
		ERROR("\"%s\" is not an ethernet interface!\n", ifh->ifa_name);
		return FALSE;
	}
	DBG("==> %s %s %s\n", ifv4->ifa_name,
	    inet_ntoa(((struct sockaddr_in *)ifv4->ifa_addr)->sin_addr),
	    ether_ntoa((struct ether_addr *)
		       LLADDR((struct sockaddr_dl *)ifh->ifa_addr))
	    );

	*communicationTechnology = PTP_ETHER;
	memcpy(ifaceName, ifh->ifa_name, IFACE_NAME_LENGTH);
	memcpy(uuid, LLADDR((struct sockaddr_dl *)ifh->ifa_addr), 
	       PTP_UUID_LENGTH);

	return ((struct sockaddr_in *)ifv4->ifa_addr)->sin_addr.s_addr;

#endif
}


/*
 * CHANGE ADD FUNCITON
 * find mac address of net devices
 */
int findMacAddr(unsigned char* addr)
{
    struct ifreq ifr;
    struct ifconf ifc;
    char buf[1024];
    int success = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1) {
        printf("failed to initialize socket\n"); //remember to chage this to
        return -1;
    };

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
        printf("ifconf ioctl error\n");
        return -1;
    }

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            if (! (ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    success = 1;
                    break;
                }
            }
        }
        else {
            printf("ifreq ioctl error \n");
            return -1;
        }
    }

    if (success) memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);
    return 0;
}


/** 
 * start all of the UDP stuff 
 * must specify 'subdomainName', and optionally 'ifaceName', 
 * if not then pass ifaceName == "" 
 * on socket options, see the 'socket(7)' and 'ip' man pages 
 *
 * @param netPath 
 * @param rtOpts 
 * @param ptpClock 
 * 
 * @return TRUE if successful
 */
Boolean 
netInit(NetPath * netPath, RunTimeOpts * rtOpts, PtpClock * ptpClock)
{
	if(!rtOpts->ethernet_mode)
	{
		int temp;
		struct in_addr interfaceAddr, netAddr;
		struct sockaddr_in addr;
		struct ip_mreq imr;
		char addrStr[NET_ADDRESS_LENGTH];

		DBG("netInit\n");

		/* open sockets */
		if ((netPath->eventSock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0
			|| (netPath->generalSock = socket(PF_INET, SOCK_DGRAM,
							  IPPROTO_UDP)) < 0) {
			PERROR("failed to initalize sockets");
			return FALSE;
		}
		/* find a network interface */
		if (!(interfaceAddr.s_addr =
			  findIface(rtOpts->ifaceName,
				&ptpClock->port_communication_technology,
				ptpClock->port_uuid_field, netPath)))
			return FALSE;

		DBG("Local IP address used : %s \n", inet_ntoa(interfaceAddr));

		temp = 1;			/* allow address reuse */
		if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_REUSEADDR,
				   &temp, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, SOL_SOCKET, SO_REUSEADDR,
				  &temp, sizeof(int)) < 0) {
			DBG("failed to set socket reuse\n");
		}
		/* bind sockets */
		/*
		 * need INADDR_ANY to allow receipt of multi-cast and uni-cast
		 * messages
		 */
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(PTP_EVENT_PORT);
		if (bind(netPath->eventSock, (struct sockaddr *)&addr,
			 sizeof(struct sockaddr_in)) < 0) {
			PERROR("failed to bind event socket");
			return FALSE;
		}
		addr.sin_port = htons(PTP_GENERAL_PORT);
		if (bind(netPath->generalSock, (struct sockaddr *)&addr,
			 sizeof(struct sockaddr_in)) < 0) {
			PERROR("failed to bind general socket");
			return FALSE;
		}
		/* send a uni-cast address if specified (useful for testing)
		 * ����netPath->unicastAddr
		 * */
		if (rtOpts->unicastAddress[0]) {
			/* Attempt a DNS lookup first. */
			struct hostent *host;
			host = gethostbyname2(rtOpts->unicastAddress, AF_INET);//ͨ��hostname��ȡhostent���ýṹ���h_addr_list����ip��ַ��Ϣ
					if (host != NULL) {
				if (host->h_length != 4) {
					PERROR("unicast host resolved to non ipv4"
						   "address");
					return FALSE;
				}
				netPath->unicastAddr =
					*(uint32_t *)host->h_addr_list[0];
			} else {
				/* Maybe it's a dotted quad. */
				//���rtOpts->unicastAddress��ʮ���Ƶĵ�ֵ�ַ��ʽ��ֱ�ӽ���ת���ɶ����Ƶ�ַ����ֵ��netAddr��netPath->unicastAddr
				if (!inet_aton(rtOpts->unicastAddress, &netAddr)) {
					ERROR("failed to encode uni-cast address: %s\n",
						  rtOpts->unicastAddress);
					return FALSE;
					netPath->unicastAddr = netAddr.s_addr;
				}
					}
			} else
					netPath->unicastAddr = 0;

		/* Init General multicast IP address
		 * ����netPath->multicastAddr
		 * �ಥ������������
		 * (1)����socket
		 * (2)����TTLֵ IP_MULTICAST_TTL
		 * (3)�����Ƿ������ػػ� IP_MULTICAST_LOOP
		 * (4)����ಥ�� IP_ADD_MEMBERSHIP
		 * (5)�������� send
		 * (6)�������� recv
		 * (7)�˳��ಥ�� IP_DROP_MEMBERSHIP
		 * ע��: TCPЭ��Ҳ�������ڶಥ.
		 * */
		memcpy(addrStr, DEFAULT_PTP_DOMAIN_ADDRESS, NET_ADDRESS_LENGTH);

		if (!inet_aton(addrStr, &netAddr)) {
			ERROR("failed to encode multi-cast address: %s\n", addrStr);
			return FALSE;
		}
		netPath->multicastAddr = netAddr.s_addr;

		/* multicast send only on specified interface */

		imr.imr_multiaddr.s_addr = netAddr.s_addr;//�㲥��ַ
		imr.imr_interface.s_addr = interfaceAddr.s_addr;//������ַ
		//����socket��IPЭ���ѡ��IP_MULTICAST_IF��ֵΪ������ַ
		if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_MULTICAST_IF,
				   &imr.imr_interface.s_addr, sizeof(struct in_addr)) < 0
			|| setsockopt(netPath->generalSock, IPPROTO_IP, IP_MULTICAST_IF,
				  &imr.imr_interface.s_addr, sizeof(struct in_addr))
			< 0) {
			PERROR("failed to enable multi-cast on the interface");
			return FALSE;
		}
		/* join multicast group (for receiving) on specified interface */
		//����socket��IPЭ���ѡ��IP_ADD_MEMBERSHIPֵΪimr
		if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				   &imr, sizeof(struct ip_mreq)) < 0
			|| setsockopt(netPath->generalSock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				  &imr, sizeof(struct ip_mreq)) < 0) {
			PERROR("failed to join the multi-cast group");
			return FALSE;
		}
		/* End of General multicast Ip address init */

		/* Init Peer multicast IP address
		 * ����PEER_PTP_DOMAIN_ADDRESS=224.0.0.107��֮ǰ�ĳ�ʼ��������ͬ����������
		 * */
		memcpy(addrStr, PEER_PTP_DOMAIN_ADDRESS, NET_ADDRESS_LENGTH);

		if (!inet_aton(addrStr, &netAddr)) {
			ERROR("failed to encode multi-cast address: %s\n", addrStr);
			return FALSE;
		}
		netPath->peerMulticastAddr = netAddr.s_addr;

		/* multicast send only on specified interface */
		imr.imr_multiaddr.s_addr = netAddr.s_addr;
		imr.imr_interface.s_addr = interfaceAddr.s_addr;
		// ���öಥ�ӿ�
		if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_MULTICAST_IF,
				   &imr.imr_interface.s_addr, sizeof(struct in_addr)) < 0
			|| setsockopt(netPath->generalSock, IPPROTO_IP, IP_MULTICAST_IF,
				  &imr.imr_interface.s_addr, sizeof(struct in_addr))
			< 0) {
			PERROR("failed to enable multi-cast on the interface");
			return FALSE;
		}

		/* join multicast group (for receiving) on specified interface
		 * ��ָ���Ľӿڼ���ಥ
		 * */
		if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				   &imr, sizeof(struct ip_mreq)) < 0
			|| setsockopt(netPath->generalSock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				  &imr, sizeof(struct ip_mreq)) < 0) {
			PERROR("failed to join the multi-cast group");
			return FALSE;
		}
		/* End of Peer multicast Ip address init */


		/* set socket time-to-live to 1
		 * ���öಥ��TTLֵΪ1�룬��ȻҲ����ͨ��ѡ��T���趨
		 * */

		if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_MULTICAST_TTL,
				   &rtOpts->ttl, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, IPPROTO_IP, IP_MULTICAST_TTL,
				  &rtOpts->ttl, sizeof(int)) < 0) {
			PERROR("failed to set the multi-cast time-to-live");
			return FALSE;
		}
		/* enable loopback
		 * ����ಥ���ݻ��͵�����loop�ӿ�
		 * */

		temp = 1;
		if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_MULTICAST_LOOP,
				   &temp, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, IPPROTO_IP, IP_MULTICAST_LOOP,
				  &temp, sizeof(int)) < 0) {
			PERROR("failed to enable multi-cast loopback");
			return FALSE;
		}
		/* make timestamps available through recvmsg()
		 * ����socket����ʹ��recvmsg����ȡʱ���
		 * */
		temp = 1;
	#if defined(linux)
		if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_TIMESTAMP,
				   &temp, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, SOL_SOCKET, SO_TIMESTAMP,
				  &temp, sizeof(int)) < 0) {
			PERROR("failed to enable receive time stamps");
			return FALSE;
		}
	#else /* FreeBSD */
		if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_BINTIME,
				   &temp, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, SOL_SOCKET, SO_BINTIME,
				  &temp, sizeof(int)) < 0) {
			PERROR("failed to enable receive time stamps");
			return FALSE;
		}
	#endif
		return TRUE;
	}
	else //	CHANGE if ethernet mode on
		// linux only!
	{
		//open sockets
		DBGV("ether net init\n");
	    struct sockaddr_ll sll;
	    struct ifreq ifr;
	    struct packet_mreq mreq;
	    bzero(&sll, sizeof(sll));
	    bzero(&ifr, sizeof(ifr));


	    if(findMacAddr(netPath->srcMac) < 0){
	         ERROR("error when searching for Mac address\n");
	        return -1;
	    }
		//change this to ETH_P_ALL hope it works
		if((netPath->eventSock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_1588))) < 0
				|| (netPath->generalSock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_1588))) < 0){
			PERROR("failed to initalize sockets");
			return FALSE;
		}
		//prepare for binding
	    //get interface first
	    strncpy((char *)ifr.ifr_name, "eth0", IFNAMSIZ);
	    if((ioctl(netPath->eventSock, SIOCGIFINDEX, &ifr)) == -1)
	    {
	        ERROR("Error getting Interface index !\n");
	        return FALSE;
	    }

	    sll.sll_family = AF_PACKET;
	    sll.sll_ifindex = ifr.ifr_ifindex;
	    sll.sll_protocol = htons(ETH_P_ALL);
	    if (bind(netPath->eventSock, (struct sockaddr *)&sll,
	    			 sizeof(struct sockaddr_ll)) < 0) {
	    			PERROR("failed to bind event socket");
	    			return FALSE;
		}

	    if((ioctl(netPath->generalSock, SIOCGIFINDEX, &ifr)) == -1)
	    {
	        ERROR("Error getting Interface index !\n");
	        return FALSE;
	    }

	    sll.sll_family = AF_PACKET;
	    sll.sll_ifindex = ifr.ifr_ifindex;
	    sll.sll_protocol = htons(ETH_P_ALL);
		if (bind(netPath->generalSock, (struct sockaddr *)&sll,
			 sizeof(struct sockaddr_ll)) < 0) {
			PERROR("failed to bind general socket");
			return FALSE;
		}

	    //set option
		int temp = 1;
		if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_REUSEADDR,
				   &temp, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, SOL_SOCKET, SO_REUSEADDR,
				  &temp, sizeof(int)) < 0) {
			DBG("failed to set socket reuse\n");
		}

		//jion multicast group
	    mreq.mr_ifindex = ifr.ifr_ifindex;
	    mreq.mr_type = PACKET_MR_MULTICAST;//PACKET_MR_ALLMULTI;//PACKET_MR_PROMISC;//
	    mreq.mr_alen = ETH_ALEN;
	    unsigned char dest_mac[6] = NON_PEER_MAC_ADDRESS;
	    mreq.mr_address[0] = dest_mac[0];
	    mreq.mr_address[1] = dest_mac[1];
	    mreq.mr_address[2] = dest_mac[2];
	    mreq.mr_address[3] = dest_mac[3];
	    mreq.mr_address[4] = dest_mac[4];
	    mreq.mr_address[5] = dest_mac[5];
	    mreq.mr_address[6] = 0x00;
	    mreq.mr_address[7] = 0x00;
		if (setsockopt(netPath->eventSock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
				   &mreq, sizeof(struct packet_mreq)) < 0
			|| setsockopt(netPath->generalSock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
				  &mreq, sizeof(struct packet_mreq)) < 0) {
			PERROR("failed to join the multi-cast group");
			return FALSE;
		}
		unsigned char dest_mac1[6] = PEER_MAC_ADDRESS;
	    mreq.mr_address[0] = dest_mac1[0];
	    mreq.mr_address[1] = dest_mac1[1];
	    mreq.mr_address[2] = dest_mac1[2];
	    mreq.mr_address[3] = dest_mac1[3];
	    mreq.mr_address[4] = dest_mac1[4];
	    mreq.mr_address[5] = dest_mac1[5];
	    mreq.mr_address[6] = 0x00;
	    mreq.mr_address[7] = 0x00;
		if (setsockopt(netPath->eventSock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
				   &mreq, sizeof(struct packet_mreq)) < 0
			|| setsockopt(netPath->generalSock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
				  &mreq, sizeof(struct packet_mreq)) < 0) {
			PERROR("failed to join the multi-cast group");
			return FALSE;
		}

		/* make timestamps available through recvmsg() */
/*		if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_TIMESTAMP,
				   &temp, sizeof(int)) < 0
			|| setsockopt(netPath->generalSock, SOL_SOCKET, SO_TIMESTAMP,
				  &temp, sizeof(int)) < 0) {
			PERROR("failed to enable receive time stamps");
			return FALSE;
		}
*/
		return TRUE;
	}
}

/* Check if data have been received
 * ��һ��������ʾselect��I/O��·ת�ӣ���Ը��ȴ�ʱ��
 * */
int 
netSelect(TimeInternal * timeout, NetPath * netPath)
{
	int ret, nfds;
	fd_set readfds;
	struct timeval tv, *tv_ptr;

	if (timeout < 0)
		return FALSE;

	FD_ZERO(&readfds);	//����readfds������λΪ0������һ��fd_set֮�����Ĳ���
	//����readfds�������б�
	FD_SET(netPath->eventSock, &readfds);
	FD_SET(netPath->generalSock, &readfds);

	//select������Ը��ȴ�ʱ��
	if (timeout) {
		tv.tv_sec = timeout->seconds;
		tv.tv_usec = timeout->nanoseconds / 1000;
		tv_ptr = &tv;
	} else
		tv_ptr = 0;//���ȴ�

	//���õ�һ����������һ�������ĺ����ǡ������������һ��
	if (netPath->eventSock > netPath->generalSock)
		nfds = netPath->eventSock;
	else
		nfds = netPath->generalSock;

	//���м�����������Ϊ��ʱ�����Ե���sleepʹ�ã�������sleepҪ��׼����ȷ��micro sec
	ret = select(nfds + 1, &readfds, 0, 0, tv_ptr) > 0;

	if (ret < 0) {	//����ֵΪ-1��ʾ����
		if (errno == EAGAIN || errno == EINTR)
			return 0;
	}
	return ret;
}




/** 
 * store received data from network to "buf" , get and store the
 * SO_TIMESTAMP value in "time" for an event message
 * 
 * @param buf 
 * @param time 
 * @param netPath 
 * 
 * @return 
 */
ssize_t 
netRecvEvent(Octet * buf, TimeInternal * time, NetPath * netPath)
{
	ssize_t ret;
	struct msghdr msg;
	struct iovec vec[1];
	struct sockaddr_in from_addr;

	union {
		struct cmsghdr cm;
		char	control[CMSG_SPACE(sizeof(struct timeval))];
	}     cmsg_un;

	struct cmsghdr *cmsg;
#if defined(linux)
	struct timeval *tv;
#else
	struct timespec ts;
#endif

	vec[0].iov_base = buf;
	vec[0].iov_len = PACKET_SIZE;

	memset(&msg, 0, sizeof(msg));
	memset(&from_addr, 0, sizeof(from_addr));
	memset(buf, 0, PACKET_SIZE);
	memset(&cmsg_un, 0, sizeof(cmsg_un));

	msg.msg_name = (caddr_t)&from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_iov = vec;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_un.control;
	msg.msg_controllen = sizeof(cmsg_un.control);
	msg.msg_flags = 0;

	ret = recvmsg(netPath->eventSock, &msg, MSG_DONTWAIT);
	if (ret <= 0) {
		if (errno == EAGAIN || errno == EINTR)
			return 0;

		return ret;
	}
	if (msg.msg_flags & MSG_TRUNC) {
		ERROR("received truncated message\n");
		return 0;
	}
	/* get time stamp of packet */
	if (!time) {
		ERROR("null receive time stamp argument\n");
		return 0;
	}
	if (msg.msg_flags & MSG_CTRUNC) {
		ERROR("received truncated ancillary data\n");
		return 0;
	}
	if (msg.msg_controllen < sizeof(cmsg_un.control)) {
		ERROR("received short ancillary data (%ld/%ld)\n",
		    (long)msg.msg_controllen, (long)sizeof(cmsg_un.control));

		return 0;
	}
#if defined(linux)
	tv = 0;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; 
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && 
		    cmsg->cmsg_type == SCM_TIMESTAMP)
			tv = (struct timeval *)CMSG_DATA(cmsg);
	}

	if (tv) {
		time->seconds = tv->tv_sec;
		time->nanoseconds = tv->tv_usec * 1000;
		DBGV("kernel recv time stamp %us %dns\n", 
		     time->seconds, time->nanoseconds);
#else /* FreeBSD has more accurate time stamps */
	bzero(&ts, sizeof(ts));
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; 
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && 
		    cmsg->cmsg_type == SCM_BINTIME)
			bintime2timespec((struct bintime *)CMSG_DATA(cmsg),
					 &ts);
	}

	if (ts.tv_sec != 0) {
		time->seconds = ts.tv_sec;
		time->nanoseconds = ts.tv_nsec;
		DBGV("kernel recv time stamp %us %dns\n", time->seconds, time->nanoseconds);
#endif /* Linux or FreeBSD */
	} else {
		/*
		 * do not try to get by with recording the time here, better
		 * to fail because the time recorded could be well after the
		 * message receive, which would put a big spike in the
		 * offset signal sent to the clock servo
		 */
		DBG("no receive time stamp\n");
		return 0;
	}

	return ret;
}
//CHANGE add mac version
ssize_t
macRecvEvent(Octet * buf, TimeInternal * time, NetPath * netPath)
{
	buf -= MAC_HEADER_LEN; //repoint it to get reserved space for mac header
    struct sockaddr_ll saddr;
    unsigned int saddr_len = sizeof(struct sockaddr_ll);
    struct timeval tv = {0,0};
    int len;

    saddr.sll_family = AF_PACKET;
    saddr.sll_protocol = htons(ETH_P_ALL);
//    packet_info.sll_ifindex = 2;
    saddr.sll_hatype   = ARPHRD_ETHER;
    saddr.sll_pkttype  = PACKET_MULTICAST;
//    if((len = recvfrom(*socket, buf, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr*)&saddr, &saddr_len)) == -1)
    if((len = recvfrom(netPath->eventSock, buf, PACKET_SIZE, 0, (struct sockaddr*)&saddr, &saddr_len)) <=0)
    {
        ERROR("Recv from returned <=0: ");
        return len;
    }

    if(*(unsigned short *)(buf+MAC_TYPE_POS) != htons(ETH_P_1588))
    {
    	//this erro gonna spam the whole log so close it
//        ERROR("no what I want\n");
        return 1;
    }

    if(ioctl(netPath->eventSock,SIOCGSTAMP,&tv)<0)
    {
        ERROR("failed to get time\n");
        return 0;
    }

	if (tv.tv_sec) {
		time->seconds = tv.tv_sec;
		time->nanoseconds = tv.tv_usec * 1000;
		DBGV("kernel recv time stamp %us %dns\n",
		     time->seconds, time->nanoseconds);
	}
	else{
		DBG("no receive time stamp\n");
		return 0;
	}
	DBGV("mac received events\n");
    return len;
}

/** 
 * 
 * store received data from network to "buf" get and store the
 * SO_TIMESTAMP value in "time" for a general message
 * 
 * @param buf 
 * @param time 
 * @param netPath 
 * 
 * @return 
 */

ssize_t 
netRecvGeneral(Octet * buf, TimeInternal * time, NetPath * netPath)
{
	ssize_t ret;
	struct msghdr msg;
	struct iovec vec[1];
	struct sockaddr_in from_addr;

	union {
		struct cmsghdr cm;
		char	control[CMSG_SPACE(sizeof(struct timeval))];
	}     cmsg_un;

	struct cmsghdr *cmsg;
#if defined(linux)
	struct timeval *tv;
#else
	struct timespec ts;
#endif 
	vec[0].iov_base = buf;
	vec[0].iov_len = PACKET_SIZE;

	memset(&msg, 0, sizeof(msg));
	memset(&from_addr, 0, sizeof(from_addr));
	memset(buf, 0, PACKET_SIZE);
	memset(&cmsg_un, 0, sizeof(cmsg_un));

	msg.msg_name = (caddr_t)&from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_iov = vec;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_un.control;
	msg.msg_controllen = sizeof(cmsg_un.control);
	msg.msg_flags = 0;

	ret = recvmsg(netPath->generalSock, &msg, MSG_DONTWAIT);
	if (ret <= 0) {
		if (errno == EAGAIN || errno == EINTR)
			return 0;

		return ret;
	}
	if (msg.msg_flags & MSG_TRUNC) {
		ERROR("received truncated message\n");
		return 0;
	}
	/* get time stamp of packet */
	if (!time) {
		ERROR("null receive time stamp argument\n");
		return 0;
	}
	if (msg.msg_flags & MSG_CTRUNC) {
		ERROR("received truncated ancillary data\n");
		return 0;
	}
	if (msg.msg_controllen < sizeof(cmsg_un.control)) {
		ERROR("received short ancillary data (%ld/%ld)\n",
		    (long)msg.msg_controllen, (long)sizeof(cmsg_un.control));

		return 0;
	}
#if defined(linux)
	tv = 0;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; 
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && 
		    cmsg->cmsg_type == SCM_TIMESTAMP)
			tv = (struct timeval *)CMSG_DATA(cmsg);
	}

	if (tv) {
		time->seconds = tv->tv_sec;
		time->nanoseconds = tv->tv_usec * 1000;
		DBGV("kernel recv time stamp %us %dns\n", 
		     time->seconds, time->nanoseconds);
#else /* FreeBSD has more accurate time stamps */
	bzero(&ts, sizeof(ts));
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; 
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && 
		    cmsg->cmsg_type == SCM_BINTIME)
			bintime2timespec((struct bintime *)CMSG_DATA(cmsg),
					 &ts);
	}

	if (ts.tv_sec != 0) {
		time->seconds = ts.tv_sec;
		time->nanoseconds = ts.tv_nsec;
		DBGV("kernel recv time stamp %us %dns\n", time->seconds, time->nanoseconds);
#endif /* Linux or FreeBSD */
	} else {
		/*
		 * do not try to get by with recording the time here, better
		 * to fail because the time recorded could be well after the
		 * message receive, which would put a big spike in the
		 * offset signal sent to the clock servo
		 */
		DBG("no receive time stamp\n");
		return 0;
	}

	return ret;
}


//mac version of netRecvGeneral
ssize_t
macRecvGeneral(Octet * buf, TimeInternal * time, NetPath * netPath)
{
	buf -= MAC_HEADER_LEN; //repoint it to get reserved space for mac header
    struct sockaddr_ll saddr;
    unsigned int saddr_len = sizeof(struct sockaddr_ll);
    struct timeval tv = {0,0};
    int len;

    saddr.sll_family = AF_PACKET;
    saddr.sll_protocol = htons(ETH_P_ALL);
//    packet_info.sll_ifindex = 2;
    saddr.sll_hatype   = ARPHRD_ETHER;
    saddr.sll_pkttype  = PACKET_MULTICAST;
    if((len = recvfrom(netPath->generalSock, buf, PACKET_SIZE, 0, (struct sockaddr*)&saddr, &saddr_len)) <=0)
    {
        ERROR("Recv from returned <=0: ");
        return len;
    }
    if(*(unsigned short *)(buf+MAC_TYPE_POS) != htons(ETH_P_1588))
    {
    	//this erro gonna spam the whole log so close it
//        ERROR("no what I want\n");
        return 1;
    }

    if(ioctl(netPath->generalSock,SIOCGSTAMP,&tv)<0)
    {
        ERROR("failed to get time\n");
        return 0;
    }
	if (tv.tv_sec) {
		time->seconds = tv.tv_sec;
		time->nanoseconds = tv.tv_usec * 1000;
		DBGV("kernel recv time stamp %us %dns\n",
		     time->seconds, time->nanoseconds);
	}
	else{
		DBG("no receive time stamp\n");
		return 0;
	}
	DBGV("mac received genernal\n");
    return len;


}
ssize_t 
netSendEvent(Octet * buf, UInteger16 length, NetPath * netPath)
{
	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_EVENT_PORT);

	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast event message\n");
		/* 
		 * Need to forcibly loop back the packet since
		 * we are not using multicast. 
		 * ����netPath->eventSock������
		 */
		addr.sin_addr.s_addr = htonl(0x7f000001);

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error looping back uni-cast event message\n");
		
	} else {
		//netPath->multicastAddrΪĬ�϶ಥ��ַ��peer-to-peer��E2E��ʱ�������ͬ
		addr.sin_addr.s_addr = netPath->multicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast event message\n");
	}

	return ret;
}
int makeSock_ll(Integer32 socket,struct sockaddr_ll *s_addr, unsigned char *dest_mac)
{

//	DBGV("making sock_ll\n");
	/*prepare sockaddr_ll*/
	struct ifreq ifr;

	bzero(s_addr, sizeof(*s_addr));

	bzero(&ifr, sizeof(ifr));

	/* First Get the Interface Index  */
/*
	char dev[4] = "eth0";
	strncpy((char *)ifr.ifr_name, dev, IFNAMSIZ);
	if((ioctl(socket, SIOCGIFINDEX, &ifr)) == -1)
	{
		DBGV("Error getting Interface index !\n");
		return(-1);
	}
	*/
//	DBGV("filling sockaddr_ll\n");
    /*RAW communication*/
	s_addr->sll_family   = PF_PACKET;
//	DBGV("halfway\n");
	s_addr->sll_protocol = htons(ETH_P_1588);
	s_addr->sll_ifindex  =  2;//ifr.ifr_ifindex;
	/*ARP hardware identifier is ethernet*/
	s_addr->sll_hatype   = ARPHRD_ETHER;
	/*target is another host*/
	s_addr->sll_pkttype  = PACKET_MULTICAST;

	/*address length*/
	s_addr->sll_halen    = ETH_ALEN;
	/*MAC - begin*/
	s_addr->sll_addr[0] = dest_mac[0];
	s_addr->sll_addr[1] = dest_mac[1];
	s_addr->sll_addr[2] = dest_mac[2];
	s_addr->sll_addr[3] = dest_mac[3];
	s_addr->sll_addr[4] = dest_mac[4];
	s_addr->sll_addr[5] = dest_mac[5];
	/*MAC - end*/
	s_addr->sll_addr[6]  = 0x00;/*not used*/
	s_addr->sll_addr[7]  = 0x00;/*not used*/
//	DBGV("filled\n");
	return 0;
}

// CHANGE add fillbuffer
void fillBuffer(Octet *buffer, Octet *dat, UInteger16 len,unsigned char *dest_mac, unsigned char *src_mac)
{
	Octet *etherhead = buffer;
	Octet *data = buffer + 14;
	struct ethhdr *eh = (struct ethhdr *) etherhead;
	/*set the frame header*/
	memcpy((void*)buffer, (void*)dest_mac, ETH_ALEN);
	memcpy((void*)(buffer+ETH_ALEN), (void*)src_mac, ETH_ALEN);
	eh->h_proto =  htons(ETH_P_1588);
	memcpy((void*)(data), (void*)dat,len);
}
// CHANGE add macSendEvent
ssize_t
macSendEvent(Octet * buf, UInteger16 length, NetPath * netPath)
{
//	DBGV("sending mac events\n");
	struct sockaddr_ll s_addr;
	Octet *buffer = (Octet *)malloc(length +14);
	ssize_t ret = 0;
	/*
	unsigned char src_mac[6];
    if(findMacAddr(src_mac) < 0){
         ERROR("error when searching for Mac address\n");
        return -1;
    }
*/
    unsigned char dest_mac[6] = NON_PEER_MAC_ADDRESS;
 //   DBGV("making sock_ll\n");
    if(makeSock_ll(netPath->generalSock,&s_addr,dest_mac)< 0)	//I really don't know why, but to general sock works, event sock fails
    	{
			DBG("error in make sockaddr_ll\n");
    	}
//    DBGV("filling buffer\n");
    fillBuffer(buffer,buf,length,dest_mac,netPath->srcMac);
	/*send the packet*/
//    DBGV("sending...\n");
	ret = sendto(netPath->generalSock, buffer, length+14, 0,
						 (struct sockaddr*)&s_addr, sizeof(s_addr));
	if (ret <= 0){
		DBG("error sending multi-cast event message\n");
	}
	free(buffer);
	return ret;
}

ssize_t 
netSendGeneral(Octet * buf, UInteger16 length, NetPath * netPath)
{
	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_GENERAL_PORT);

	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast general message\n");
	} else {
		addr.sin_addr.s_addr = netPath->multicastAddr;

		ret = sendto(netPath->generalSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast general message\n");
	}
	return ret;
}
// CHANGE add macSendGeneral
ssize_t
macSendGeneral(Octet * buf, UInteger16 length, NetPath * netPath)
{
	struct sockaddr_ll s_addr;
	Octet *buffer = (Octet *)malloc(length +14);
	ssize_t ret = 0;
	/*
	unsigned char src_mac[6];
    if(findMacAddr(src_mac) < 0){
         ERROR("error when searching for Mac address\n");
        return -1;
    }
*/
    unsigned char dest_mac[6] = NON_PEER_MAC_ADDRESS;
    if(makeSock_ll(netPath->generalSock, &s_addr, dest_mac)< 0) DBG("error in make sockaddr_ll\n");

    fillBuffer(buffer,buf,length,dest_mac,&netPath->srcMac[0]);
	/*send the packet*/
	ret = sendto(netPath->generalSock, buffer, length+14, 0,
						 (struct sockaddr*)&s_addr, sizeof(s_addr));
	if (ret <= 0){
		DBG("error sending multi-cast general message\n");
	}
	free(buffer);
	return ret;
}

ssize_t 
netSendPeerGeneral(Octet * buf, UInteger16 length, NetPath * netPath)
{

	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_GENERAL_PORT);

	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast general message\n");

	} else {
		addr.sin_addr.s_addr = netPath->peerMulticastAddr;

		ret = sendto(netPath->generalSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast general message\n");
	}
	return ret;

}
// CHANGE add macSendPeerGenernal
ssize_t
macSendPeerGeneral(Octet * buf, UInteger16 length, NetPath * netPath)
{
	struct sockaddr_ll s_addr;
	Octet *buffer = (Octet *)malloc(length +14);
	ssize_t ret = 0;
	/*
	unsigned char src_mac[6];
    if(findMacAddr(src_mac) < 0){
         ERROR("error when searching for Mac address\n");
        return -1;
    }
*/
    unsigned char dest_mac[6] = PEER_MAC_ADDRESS;
    if(makeSock_ll(netPath->generalSock, &s_addr,dest_mac)< 0) DBG("error in make sockaddr_ll\n");

    fillBuffer(buffer,buf,length,dest_mac,netPath->srcMac);
	/*send the packet*/
	ret = sendto(netPath->generalSock, buffer, length+14, 0,
						 (struct sockaddr*)&s_addr, sizeof(s_addr));
	if (ret <= 0){
		DBG("error sending multi-cast peer peer general message\n");
	}
	free(buffer);
	return ret;
}


ssize_t 
netSendPeerEvent(Octet * buf, UInteger16 length, NetPath * netPath)
{
	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_EVENT_PORT);

	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast event message\n");
	} else {
		addr.sin_addr.s_addr = netPath->peerMulticastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast event message\n");
	}
	return ret;
}

// CHANGE add macSendPeerEvent
ssize_t
macSendPeerEvent(Octet * buf, UInteger16 length, NetPath * netPath)
{
	struct sockaddr_ll s_addr;
	Octet *buffer = (Octet *)malloc(length +14);
	ssize_t ret = 0;
/*
	unsigned char src_mac[6];
    if(findMacAddr(src_mac) < 0){
         ERROR("error when searching for Mac address\n");
        return -1;
    }
*/
    unsigned char dest_mac[6] = PEER_MAC_ADDRESS;
    if(makeSock_ll(netPath->generalSock, &s_addr,dest_mac)< 0) DBG("error in make sockaddr_ll\n");

    fillBuffer(buffer,buf,length,dest_mac,netPath->srcMac);
	/*send the packet*/
	ret = sendto(netPath->generalSock, buffer, length+14, 0,
						 (struct sockaddr*)&s_addr, sizeof(s_addr));
	if (ret <= 0){
		DBG("error sending multi-cast peer event message\n");
	}
	free(buffer);
	return ret;
}

