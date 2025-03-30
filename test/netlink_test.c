#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

#include <netlink/msg.h>

#include <lldp_util.h>

extern int get_mac2(int s, const char *ifname, u8 mac[], bool perm_mac);

// Needed to capture SIOCGIFINDEX for translating ifname to index
int __wrap_ioctl(int fd, unsigned long op, void *arg)
{
	fprintf(stderr, "ioctl(%d, %lu, %p)\n", fd, op, arg);
	assert(op == SIOCGIFINDEX);

	return 0;
}

struct nl_msg *build_getlink_msg(uint8_t mac[6], char *kind, uint8_t slave_mac[6])
{
	struct nl_msg *msg = NULL;
	struct ifinfomsg ifi;
	struct nlattr *opts;
	struct nlattr *opts2;
	struct nl_addr *addr = NULL;

	memset(&ifi, 0U, sizeof(ifi));
	if((msg = nlmsg_alloc_simple(RTM_GETLINK, 0)) == NULL)
		goto out_err;
	if(nlmsg_append(msg, &ifi, sizeof(ifi), NLMSG_ALIGNTO) != 0)
		goto out_err;
	NLA_PUT_STRING(msg, IFLA_IFNAME, "eth0");
	//assert(sizeof(mac) == 6);
	//NLA_PUT(msg, IFLA_ADDRESS, sizeof(mac), mac);
	if((addr = nl_addr_build(AF_LLC, mac, 6)) == NULL)
		goto out_err;
	NLA_PUT_ADDR(msg, IFLA_ADDRESS, addr);
	nl_addr_put(addr);
	addr = NULL;

	if(kind || slave_mac) {
		if((opts = nla_nest_start(msg, IFLA_LINKINFO)) == NULL)
			goto out_err;

		if((opts2 = nla_nest_start(msg, IFLA_INFO_SLAVE_DATA)) == NULL)
			goto out_err;

		if((addr = nl_addr_build(AF_LLC, slave_mac, 6)) == NULL)
			goto out_err;
		NLA_PUT_ADDR(msg, IFLA_BOND_SLAVE_PERM_HWADDR, addr);
		nl_addr_put(addr);
		addr = NULL;
		nla_nest_end(msg, opts2);
		if(kind)
			NLA_PUT_STRING(msg, IFLA_INFO_SLAVE_KIND, kind);
		nla_nest_end(msg, opts);
	}

	return msg;

nla_put_failure:
out_err:
	nl_addr_put(addr);
	nlmsg_free(msg);

	return NULL;
}

static const uint8_t dummy_mac[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint8_t dummy2_mac[] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

int main()
{
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int socket_pair[2];
	//pthread_t sender_thread_id, receiver_thread_id;
	int ret;

	struct nl_msg *msg = NULL;

	msg = build_getlink_msg(dummy_mac, NULL, NULL);
	assert(msg != NULL);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair) == -1) {
		perror("socketpair failed");
		exit(EXIT_FAILURE);
	}

	send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0);

	//printf("Before: %02x:%02x:%02x:%02x:%02x:%02x\n", result_mac[0], result_mac[1], result_mac[2], result_mac[3], result_mac[4], result_mac[5]);
	memset(result_mac, 0U, sizeof(result_mac));
	ret = get_mac2(socket_pair[0], "eth0", result_mac, false);
	//printf("After: %02x:%02x:%02x:%02x:%02x:%02x\n", result_mac[0], result_mac[1], result_mac[2], result_mac[3], result_mac[4], result_mac[5]);
	assert(ret == 0);
	assert(memcmp(result_mac, dummy_mac, sizeof(result_mac)) == 0);
	close(socket_pair[1]);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair) == -1) {
		perror("socketpair failed");
		exit(EXIT_FAILURE);
	}

	send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0);

	nlmsg_free(msg);
	
	memset(result_mac, 0U, sizeof(result_mac));
	ret = get_mac2(socket_pair[0], "eth0", result_mac, true);
	assert(ret == 0);
	assert(memcmp(result_mac, dummy_mac, sizeof(result_mac)) == 0);
	close(socket_pair[1]);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair) == -1) {
		perror("socketpair failed");
		exit(EXIT_FAILURE);
	}

	msg = build_getlink_msg(dummy_mac, "bond", dummy2_mac);
	assert(msg != NULL);

	send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0);

	memset(result_mac, 0U, sizeof(result_mac));
	ret = get_mac2(socket_pair[0], "eth0", result_mac, true);
	assert(ret == 0);
	assert(memcmp(result_mac, dummy2_mac, sizeof(result_mac)) == 0);
	close(socket_pair[1]);

	nlmsg_free(msg);
	
	printf("Test passed!\n");

	return 0;
}
