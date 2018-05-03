#ifndef __DBI_OVER_TCP_H__
#define __DBI_OVER_TCP_H__


struct dbi_tcp_config
{
    int port;
    int *cancel;

};

/* dbi_over_tcp main thread */
int *tcp_dbi_thread(struct dbi_tcp_config *conf);

#endif
