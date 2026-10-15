/* SPDX-License-Identifier: MIT */
#include <c_api.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
	ncnn_net_t net;
	int a, b, c, d;
	if (argc != 6)
		return (64);
	net = ncnn_net_create();
	if (net == NULL)
		return (70);
	a = ncnn_net_load_param(net, argv[1]);
	ncnn_net_clear(net);
	b = ncnn_net_load_param(net, argv[2]);
	ncnn_net_clear(net);
	c = ncnn_net_load_param(net, argv[3]);
	d = c == 0 ? ncnn_net_load_model(net, argv[4]) : 0;
	ncnn_net_clear(net);
	c = c == 0 ? ncnn_net_load_param(net, argv[3]) : c;
	if (c == 0)
		c = ncnn_net_load_model(net, argv[5]);
	ncnn_net_destroy(net);
	printf("ncnn direct API returns: malformed-magic=%d missing-param=%d missing-weights=%d truncated-weights=%d\n", a, b, d, c);
	return (a != 0 && b != 0 && d != 0 && c != 0 ? 0 : 1);
}
