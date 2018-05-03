#ifndef __DBI_OVER_UDP_H__
#define __DBI_OVER_UDP_H__

struct dbi_udp_config
{
    int port;
    int *cancel;

};

/* dbi_over_udp main thread */
int *udp_dbi_thread(struct dbi_udp_config *conf);

#endif
