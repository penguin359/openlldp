#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

#include <netlink/msg.h>

#include <check.h>

#include <lldp_util.h>

extern int get_mac2(int s, const char *ifname, u8 mac[], bool perm_mac);

// Needed to capture SIOCGIFINDEX for translating ifname to index
int __wrap_ioctl(int fd, unsigned long op, void *arg)
{
	fprintf(stderr, "ioctl(%d, %lu, %p)\n", fd, op, arg);
	ck_assert_int_eq(SIOCGIFINDEX, op);

	return 0;
}

struct nl_msg *build_getlink_msg(const uint8_t mac[6], const char *kind, const uint8_t slave_mac[6])
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
	//ck_assert_int_eq(6, sizeof(mac));
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

START_TEST(test_basic_get_mac)
{
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int socket_pair[2];
	int ret;

	struct nl_msg *msg = NULL;

	msg = build_getlink_msg(dummy_mac, NULL, NULL);
	ck_assert_ptr_nonnull(msg);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair) < 0) {
		perror("socketpair failed");
		exit(EXIT_FAILURE);
	}

	if(send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0) < 0) {
		perror("send() failed");
		exit(EXIT_FAILURE);
	}

	//printf("Before: %02x:%02x:%02x:%02x:%02x:%02x\n", result_mac[0], result_mac[1], result_mac[2], result_mac[3], result_mac[4], result_mac[5]);
	ret = get_mac2(socket_pair[0], "eth0", result_mac, false);
	close(socket_pair[1]);
	//printf("After: %02x:%02x:%02x:%02x:%02x:%02x\n", result_mac[0], result_mac[1], result_mac[2], result_mac[3], result_mac[4], result_mac[5]);
	ck_assert_int_eq(0, ret);
	ck_assert_mem_eq(dummy_mac, result_mac, sizeof(result_mac));

	nlmsg_free(msg);
}
END_TEST

START_TEST(test_ext_get_mac)
{
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int socket_pair[2];
	int ret;

	struct nl_msg *msg = NULL;

	msg = build_getlink_msg(dummy_mac, NULL, NULL);
	ck_assert_ptr_nonnull(msg);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair) < 0) {
		perror("socketpair failed");
		exit(EXIT_FAILURE);
	}

	if(send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0) < 0) {
		perror("send() failed");
		exit(EXIT_FAILURE);
	}

	ret = get_mac2(socket_pair[0], "eth0", result_mac, true);
	close(socket_pair[1]);
	ck_assert_int_eq(0, ret);
	ck_assert_mem_eq(dummy_mac, result_mac, sizeof(result_mac));

	nlmsg_free(msg);
}
END_TEST

START_TEST(test_bond_get_mac)
{
	uint8_t result_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int socket_pair[2];
	int ret;

	struct nl_msg *msg = NULL;

	msg = build_getlink_msg(dummy_mac, "bond", dummy2_mac);
	ck_assert_ptr_nonnull(msg);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socket_pair) < 0) {
		perror("socketpair failed");
		exit(EXIT_FAILURE);
	}

	if(send(socket_pair[1], nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, 0) < 0) {
		perror("send() failed");
		exit(EXIT_FAILURE);
	}

	ret = get_mac2(socket_pair[0], "eth0", result_mac, true);
	close(socket_pair[1]);
	ck_assert_int_eq(0, ret);
	ck_assert_mem_eq(dummy2_mac, result_mac, sizeof(result_mac));

	nlmsg_free(msg);
}
END_TEST

Suite *netlink_suite(void)
{
	Suite *s;
	TCase *tc_get_mac;

	s = suite_create("Netlink");

	tc_get_mac = tcase_create("Get MAC");

	tcase_add_test(tc_get_mac, test_basic_get_mac);
	tcase_add_test(tc_get_mac, test_ext_get_mac);
	tcase_add_test(tc_get_mac, test_bond_get_mac);
	suite_add_tcase(s, tc_get_mac);

	return s;
}

int main()
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = netlink_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
