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
	ck_assert_int_eq(SIOCGIFINDEX, op);

	return 0;
}

struct nl_msg *build_getlink_msg(struct nl_addr *addr, const char *kind, struct nl_addr *slave_addr)
{
	struct nl_msg *msg = NULL;
	struct ifinfomsg ifi;
	struct nlattr *opts;
	struct nlattr *opts2;

	memset(&ifi, 0U, sizeof(ifi));
	if((msg = nlmsg_alloc_simple(RTM_GETLINK, 0)) == NULL)
		goto out_err;
	if(nlmsg_append(msg, &ifi, sizeof(ifi), NLMSG_ALIGNTO) != 0)
		goto out_err;
	NLA_PUT_STRING(msg, IFLA_IFNAME, "eth0");
	NLA_PUT_ADDR(msg, IFLA_ADDRESS, addr);

	if(kind || slave_addr) {
		if((opts = nla_nest_start(msg, IFLA_LINKINFO)) == NULL)
			goto out_err;

		if((opts2 = nla_nest_start(msg, IFLA_INFO_SLAVE_DATA)) == NULL)
			goto out_err;

		NLA_PUT_ADDR(msg, IFLA_BOND_SLAVE_PERM_HWADDR, slave_addr);
		nla_nest_end(msg, opts2);
		if(kind)
			NLA_PUT_STRING(msg, IFLA_INFO_SLAVE_KIND, kind);
		nla_nest_end(msg, opts);
	}

	return msg;

nla_put_failure:
out_err:
	nlmsg_free(msg);

	return NULL;
}

static const uint8_t dummy_mac[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint8_t dummy2_mac[] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

static struct nl_addr *dummy_addr = NULL;
static struct nl_addr *dummy2_addr = NULL;

static int socket_pair[2];

#define ck_assert_addr_eq(a, b)		do { \
	char buf1[256]; \
	char buf2[256]; \
	ck_assert_msg(nl_addr_cmp((a), (b)) == 0, \
		      "Expected addr %s != actual addr %s", \
		      nl_addr2str((a), buf1, sizeof(buf1)), \
		      nl_addr2str((b), buf2, sizeof(buf2))); \
    } while(0)

#define ck_assert_mac_is_addr(mac, addr)  do { \
	struct nl_addr *mac_addr; \
	ck_assert_ptr_nonnull(mac_addr = nl_addr_build(AF_LLC, (mac), sizeof(mac))); \
	ck_assert_addr_eq((addr), mac_addr); \
	nl_addr_put(mac_addr); \
    } while(0)


void netlink_setup(void)
{
	ck_assert_int_eq(0, socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair));

	ck_assert_ptr_nonnull(dummy_addr = nl_addr_build(AF_LLC, dummy_mac, sizeof(dummy_mac)));
	ck_assert_ptr_nonnull(dummy2_addr = nl_addr_build(AF_LLC, dummy2_mac, sizeof(dummy2_mac)));
}

void netlink_teardown(void)
{
	// socket_pair[0] is expected to be closed by get_mac()
	close(socket_pair[1]);

	nl_addr_put(dummy_addr);
	nl_addr_put(dummy2_addr);
}


#suite Netlink

#tcase Get MAC

#test test_basic_get_mac
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int ret;

	struct nl_msg *msg = build_getlink_msg(dummy_addr, NULL, NULL);
	ck_assert_ptr_nonnull(msg);

	if(send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0) < 0) {
		perror("send() failed");
		exit(EXIT_FAILURE);
	}

	//printf("Before: %02x:%02x:%02x:%02x:%02x:%02x\n", result_mac[0], result_mac[1], result_mac[2], result_mac[3], result_mac[4], result_mac[5]);
	ret = get_mac2(socket_pair[0], "eth0", result_mac, false);
	//printf("After: %02x:%02x:%02x:%02x:%02x:%02x\n", result_mac[0], result_mac[1], result_mac[2], result_mac[3], result_mac[4], result_mac[5]);
	ck_assert_int_eq(0, ret);
	ck_assert_mac_is_addr(result_mac, dummy_addr);

	nlmsg_free(msg);

#test test_ext_get_mac
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int ret;

	struct nl_msg *msg = build_getlink_msg(dummy_addr, NULL, NULL);
	ck_assert_ptr_nonnull(msg);

	if(send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0) < 0) {
		perror("send() failed");
		exit(EXIT_FAILURE);
	}

	ret = get_mac2(socket_pair[0], "eth0", result_mac, true);
	ck_assert_int_eq(0, ret);
	ck_assert_mac_is_addr(result_mac, dummy_addr);

	nlmsg_free(msg);

#test test_bond_get_mac
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int ret;

	struct nl_msg *msg = build_getlink_msg(dummy_addr, "bond", dummy2_addr);
	ck_assert_ptr_nonnull(msg);

	if(send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0) < 0) {
		perror("send() failed");
		exit(EXIT_FAILURE);
	}

	ret = get_mac2(socket_pair[0], "eth0", result_mac, true);
	ck_assert_int_eq(0, ret);
	ck_assert_mac_is_addr(result_mac, dummy2_addr);

	nlmsg_free(msg);

#main-pre
	tcase_add_checked_fixture(tc1_1, netlink_setup, netlink_teardown);
