/**
 * transport_ctl.c
 *
 *  Created on: Jan 12, 2012
 *  Author: CrossLine Media, LLC <http://www.clmedia.ru>
 */

#undef DEBUG_PRFX
#include <x_config.h>
#if TRACE_XICECTL_ON
#define DEBUG_PRFX "[__gglectl] "
#endif

#include <xwlib/x_types.h>
#include <xwlib/x_utils.h>
#include <xwlib/x_obj.h>

#include <rtp.h>

#include "sessions.h"

/**
 * Starting port number for rtp port allocation
 */
#define BASE_RTP_PORT 34100
#define BASE_RTP_PORT_RETRY 4

/**
 * @todo Create port allocator service
 */
static uint16_t current_rtp_portnum = BASE_RTP_PORT;

static int
__gglectl_msg_tx(int sockfd, struct sockaddr *to, socklen_t tolen,
    struct sockaddr *from, socklen_t fromlen, int flags, char *data,
    size_t datasiz);

static void
route_rtcp_packet_in(x_io_stream *ice, void *rawrctp, int rtcplen)
{
}

static void
route_rtp_packet_in(x_io_stream *ice, rtp_hdr_t *rtp, int rtplen)
{
  char tmpstr[32];
  x_obj_attr_t hints =
    { 0, 0, 0 };

  x_object *tmp;
  ENTER;

  x_snprintf(tmpstr, sizeof(tmpstr) - 1, "%d", rtp->octet2.control2.pt);

  setattr(_XS("id"), tmpstr, &hints);

  if (!ice->media_owner_cache[rtp->octet2.control2.pt])
    {
      tmp = _CHLD(_PARNT(X_OBJECT(ice)), MEDIA_IN_CLASS_STR);
//#error "FIXME"
      ice->media_owner_cache[rtp->octet2.control2.pt] = _CHLD(tmp, tmpstr);
      if (!ice->media_owner_cache[rtp->octet2.control2.pt])
        {
          TRACE("No such media type: %d; ufrag='%s'\n",
              rtp->octet2.control2.pt, ice->ownufrag);
          ERROR;
          return;
        }

      _REFGET(ice->media_owner_cache[rtp->octet2.control2.pt]);
    }

  TRACE("Writing %d bytes to media app %d\n", rtplen, rtp->octet2.control2.pt);

  _WRITE(ice->media_owner_cache[rtp->octet2.control2.pt], &rtp->payload[0],
      rtplen - ((long long) &rtp->payload[0] - (long long) (void *) rtp),
      &hints);

  attr_list_clear(&hints);

  EXIT;
}

static int
__netaddr_equals(struct sockaddr *a1, struct sockaddr *a2)
{
  if (a1->sa_family != a2->sa_family)
    return 0; // false

  if (a1->sa_family == AF_INET)
    {
      if (((struct sockaddr_in*) a1)->sin_port
          == ((struct sockaddr_in*) a2)->sin_port
          && ((struct sockaddr_in*) a1)->sin_addr.s_addr
              == ((struct sockaddr_in*) a2)->sin_addr.s_addr)
        return 1;
    }
  else if (a1->sa_family == AF_INET6)
    {
      if (((struct sockaddr_in6*) a1)->sin6_port
          == ((struct sockaddr_in6*) a2)->sin6_port
          && !memcmp(&((struct sockaddr_in6*) a1)->sin6_addr,
              &((struct sockaddr_in6*) a2)->sin6_addr, sizeof(struct in6_addr)))
        return 1;
    }
  return 0;
}

static int
__on_packet_in(x_io_stream *ice, struct sockaddr *to, socklen_t tolen,
    struct sockaddr *from, socklen_t fromlen, int flags, char *buf, int len)
{
  int err = 0;
  struct x_candidate_pair **pair;
  unsigned int is_rtp;
  unsigned int portnum;
  int remoteportnum;
  stun_hdr_t *stn_resp;
  const char *lkey = ice->ownpwd;
  int sockfd = -1;
  int sockfd6 = -1;

  portnum = ntohs(((struct sockaddr_in *) from)->sin_port);

#if 0
  is_rtp = ~(portnum & ((unsigned int) 1));
#else
  is_rtp = ~(flags & 0x1);
#endif

  sockfd = is_rtp ? ice->sockv4 : ice->sockv4_ctl;
  sockfd6 = is_rtp ? ice->sockv6 : ice->sockv6_ctl;

  if (((stun_hdr_t *) buf)->ver_type.raw == htons(STUN_REQ))
    {
      TRACE("Received STUN REQUEST from %s:%d\n",
          inet_ntoa(((struct sockaddr_in *)from)->sin_addr), ntohs(((struct sockaddr_in *)from)->sin_port));

      stn_resp = stun_get_response(lkey, (stun_hdr_t *) buf, from);

      sendto(sockfd, stn_resp, sizeof(stun_hdr_t) + ntohs(stn_resp->mlen), 0,
          (struct sockaddr *) from, (socklen_t) fromlen);
      free(stn_resp);
    }
  else if (((stun_hdr_t *) buf)->ver_type.raw == htons(STUN_RESP))
    {

      TRACE("Received STUN RESPONSE from %x:%d to %x\n",
          ((struct sockaddr_in *)from)->sin_addr.s_addr, ntohs(((struct sockaddr_in *)from)->sin_port), ((struct sockaddr_in *)to)->sin_addr.s_addr);

      /**
       * @todo Add transaction ID checking here
       * and validate against it.
       */

      /* If non-Classic STUN message */
      if (((stun_hdr_t *) buf)->mcookie == STUN_NET_M_COOKIE)
        {
          /*
           * Find candidates pair of this response.
           * Add new candidate if not known.
           */
          for (pair = &ice->c_pairs[0];
              pair < ice->c_pairs_top && pair < &ice->c_pairs[MAX_CONN_PAIRS]
                  && *pair; pair++)
            {
#if 0 // DEBUG
              char addr1[64] =
                { 0};
              char addr2[64] =
                { 0};
              char addr3[64] =
                { 0};

              x_inet_ntop(AF_INET, &((struct sockaddr_in *) from)->sin_addr,
                  &addr1[0], sizeof(addr1) - 1);
              x_inet_ntop(AF_INET,
                  &((struct sockaddr_in *) &(*pair)->local->addr.s.addr)->sin_addr,
                  &addr2[0], sizeof(addr2) - 1);
              x_inet_ntop(AF_INET,
                  &((struct sockaddr_in *) &(*pair)->remote->addr.s.addr)->sin_addr,
                  &addr3[0], sizeof(addr3) - 1);

              TRACE("Validating %s:%d in %s:%d <=> %s:%d\n",
                  &addr1[0], ntohs(((struct sockaddr_in *)from)->sin_port), &addr2[0], ntohs(((struct sockaddr_in *)&(*pair)->local->addr.s.addr)->sin_port), &addr3[0], ntohs(((struct sockaddr_in *)&(*pair)->remote->addr.s.addr)->sin_port));
#endif
              if ((*pair)->state == NICE_CHECK_IN_PROGRESS)
                {
                  remoteportnum =
                      ntohs(
                          ((struct sockaddr_in *) &(*pair)->local->addr.s.addr)->sin_port);

                  if (__netaddr_equals(&(*pair)->remote->addr.s.addr, from)
                      && ((is_rtp && !(remoteportnum & 0x1))
                          || (!is_rtp && (remoteportnum & 0x1))))
                    {
//                      TRACE("Succeeded new pair... %s:%d <=> %s:%d\n",
//                          &addr2[0], ntohs(((struct sockaddr_in *) &(*pair)->local->addr.s.addr)->sin_port), &addr3[0], ntohs(((struct sockaddr_in *)&(*pair)->remote->addr.s.addr)->sin_port));
                      (*pair)->state = NICE_CHECK_SUCCEEDED;
                      if(!(ice->xobj.cr & ICE_FLAG_RTP_SUCCEEDED))
                      {
                          x_object *tmp;
                          ice->xobj.cr |= ICE_FLAG_RTP_SUCCEEDED;
                          if (!ice->c_pair_last_success)
                              ice->c_pair_last_success = *pair;

//                          tmp = _GNEW(_XS("$message"),NULL);
//                          BUG_ON(!tmp);
//                          _SETNM(tmp, _XS("transport-ready"));
//                          _MCAST(ice,tmp)
                      }
                      break;
                    }
                }
            }
        }
      /* Classic STUN response */
//      else
        {
          // parse stun packet
          int ok = 0;
          x_object *tmp;
          stun_packet_t *_stun_pkt = stun_packet_from_hdr((stun_hdr_t *) buf,
              NULL);
          int anum = 0; /* attribute counter */
          char trans_id[16];
          char addrstrbuf[48];
          char portbuf[12];
          uint16_t portnum;
          struct in_addr mapaddr;

          for (; anum < _stun_pkt->attrnum; anum++)
            {
              switch (_stun_pkt->attrs[anum].type)
                {
              case 0x4:
              case 0x5:
                break;
              case 0x1:
                {
                  portnum = *(uint16_t *) &_stun_pkt->attrs[anum].data[2];
                  portnum = ntohs(portnum);
                  mapaddr = *(struct in_addr *) &_stun_pkt->attrs[anum].data[4];

                  x_inet_ntop(AF_INET, &mapaddr, &addrstrbuf[0],
                      sizeof(addrstrbuf) - 1);

                  x_memcpy(&trans_id[0], &((stun_hdr_t *) buf)->mcookie, 16);
                  TRACE(
                      "My mapped address: %s:%d (trans_id='%08x%08x%08x%08x')\n",
                      addrstrbuf, portnum, *(uint32_t *)&trans_id[0], *(uint32_t *)&trans_id[4], *(uint32_t *)&trans_id[8]);
                  ok = 1;
                }
                break;
              default:
                break;
                }

              if (ok)
                break;
            }
          stun_pkt_free(_stun_pkt);

          /* if we have mapped address add to candidate list */
          if (ok)
            {
              const char *a1, *a2;
              x_object *o1, *o2, *o3;
              x_sprintf(portbuf, "%d", portnum);

              // find candidate with same attributes
              for (o1 = _CHLD(ice,_XS("local-candidate")); o1; o1 = _NXT(o1))
                {
                  a1 = _AGET(o1,_XS("ip"));
                  a1 = a1 ? a1 : _AGET(o1,_XS("address")); // for google transport
                  a2 = _AGET(o1,_XS("port"));
                  if (a1 && a2 && EQ(a1,addrstrbuf) && EQ(a2,portbuf))
                    {
                      /* already exists */
                      ok = 0;
                      break;
                    }
                }

              /* add if not found */
              if (ok)
                {
                  tmp = _GNEW(_XS("$message"),NULL);
                  BUG_ON(!tmp);

                  _SETNM(tmp, _XS("local-candidate"));

                  _ASET(tmp, _XS("address"), &addrstrbuf[0]);
                  _ASET(tmp, _XS("port"), &portbuf[0]);
                  _ASET(tmp, _XS("rel-addr"), &addrstrbuf[0]);
                  _ASET(tmp, _XS("rel-port"), &portbuf[0]);
                  _ASET(tmp, _XS("type"), _XS("srflx"));
                  _ASET(tmp, _XS("protocol"), _XS("udp"));
                  _ASET(tmp, _XS("network"), _XS("0"));

                  if ((portnum & 0x1))
                    _ASET(tmp, _XS("component"), _XS("2"));
                  else
                    _ASET(tmp, _XS("component"), _XS("1"));

                  _ASET(tmp, _XS("generation"), _XS("0"));
                  _ASET(tmp, _XS("foundation"), _XS("0"));
                  _ASET(tmp, _XS("priority"), _XS("1694498815"));

                  _ASET(tmp, _XS("password"), ice->ownpwd);
                  _ASET(tmp, _XS("username"), ice->ownufrag);

                  _INS(X_OBJECT(ice), tmp);
                }
            }
        }
    }
  else if (buf[0])
    {
      TRACE("RX: RTP/RTCP packet len=%d\n", len);
      if (is_rtp)
        {
#if 0
          x_string_t mtype = _ENV(X_OBJECT(ice),_XS("mtype"));
          TRACE("Media type: '%s'\n", mtype);

          if (mtype && EQ(mtype, _XS("audio")))
            {
              TRACE("Echo echoing AUDIO to socket %d, portnum=%d...\n",
                  sockfd, portnum);
              /* echo audio back */
              sendto(sockfd, buf, len, 0, from, fromlen);
//              __gglectl_msg_tx(sockfd, from, fromlen, to, tolen, flags, buf,
//                  len);
            }
          else
#endif

            //  TRACE("Received RTP packet: echoing...\n");
            //          x_object_try_write(X_OBJECT(ice), buf, len, NULL);
            route_rtp_packet_in(ice, buf, len);
        }
      else
        {
          TRACE("Received RTCP packet\n");
          route_rtcp_packet_in(ice, buf, len);
        }
    }
  return err;
}

static int
__gglectl_msg_rx(x_io_stream *ice, struct sockaddr *to, socklen_t tolen,
    struct sockaddr *from, socklen_t fromlen, int flags, char *data,
    size_t datasiz)
{
  TRACE("RX: len=%d\n", datasiz);
  __on_packet_in(ice, to, tolen, from, fromlen, flags, data, datasiz);
  return datasiz;
}

static int
__gglectl_recvmsg(x_object *ice, int s, int flags, struct sockaddr *saddr,
    socklen_t *addrlen)
{
  int err = 0;
  struct iovec iov[1];
  _gobee_iomsg_t mh;
  char buf[0x800];
  char cmsbuf[0x100];
  struct cmsghdr *cmsg;
  struct in_pktinfo *pktinfo;
  struct sockaddr_in caddr;

  int __flags = (s & 0x1) << 0; // set control flag

//  ENTER;

  iov[0].iov_base = &buf[0];
  iov[0].iov_len = sizeof(buf);

#if 1
  _gobee_iomsg_name(&mh) = saddr;
  _gobee_iomsg_namelen(&mh) = *addrlen;
#else
  _gobee_iomsg_name(&mh) = NULL;
  _gobee_iomsg_namelen(&mh) = 0;
#endif
  _gobee_iomsg_iov(&mh) = iov;
  _gobee_iomsg_iovlen(&mh) = 1;
  _gobee_iomsg_control(&mh) = &cmsbuf[0];
  _gobee_iomsg_controllen(&mh) = sizeof(cmsbuf);

  /* clear buffer */
  x_memset(&cmsbuf[0], 0, sizeof(cmsbuf));

  err = x_recv_msg(s, &mh, flags);

  if (err <= -1)
    {
      goto _RECV_EXIT_;
    }

  for (cmsg = CMSG_FIRSTHDR(&mh); cmsg != NULL; cmsg = CMSG_NXTHDR(&mh, cmsg))
    {
#ifdef IP_PKTINFO
      if (cmsg->cmsg_level != IPPROTO_IP && cmsg->cmsg_type != IP_PKTINFO)
        {
          continue;
        }
#endif /* LINUX */
#ifndef WIN32
      pktinfo = (struct in_pktinfo *) CMSG_DATA(cmsg);
            TRACE("ipi_spec_dst=%s, ipi_addr=%s, source addr=%s:%d\n",
                inet_ntoa(pktinfo->ipi_spec_dst),
                inet_ntoa(pktinfo->ipi_addr),
                inet_ntoa(((struct sockaddr_in *)saddr)->sin_addr),
                ntohs(((struct sockaddr_in *)saddr)->sin_port)
                  );
      caddr.sin_addr = pktinfo->ipi_spec_dst;
#endif
      break;
    }

  /**
   * @BUG invalid source address parameter!!
   * @todo to and from addresses must be correct here
   */
  iov[0].iov_len = err;
  __gglectl_msg_rx((x_io_stream *) (void *) ice, (struct sockaddr *) &caddr,
      sizeof(struct sockaddr_in), saddr, *addrlen, __flags, iov[0].iov_base,
      iov[0].iov_len);

  _RECV_EXIT_:
  //  EXIT;
  return err;
}

static int
__icestun_xobj_to_sockaddr_in(x_object *xobj, struct sockaddr_in *addr)
{
  int res = -1;
  const char *portnum;
  const char *ipaddr;

  portnum = _AGET(xobj, "port");
  ipaddr = _AGET(xobj, "ip");
  if (!ipaddr)
    ipaddr = _AGET(xobj, "address");

  if (portnum && ipaddr)
    {
      addr->sin_family = AF_INET;
      addr->sin_port = htons(atoi(portnum));
      res = !x_inet_aton(ipaddr, &addr->sin_addr);
    }

  return res;
}

static void
__icestun_resend_candidates(x_io_stream *ice)
{
  struct x_candidate_pair **pair;
  stun_hdr_t *stunmsg;
  size_t stunmsg_len;
  const char *_ufrag;
  uint16_t portnum;
  int sock;
  char buf[128];

  _ufrag = ice->ownufrag;

  if (!_ufrag)
    {
      TRACE("Invalid session state: No local transport username...\n");
    }

#ifndef CS_DISABLED
//  CS_LOCK(CS2CS(ice->_c_pairs_lock));
//  TRACE("locked candidate lock:\n");
#endif

  for (pair = &ice->c_pairs[0];
      pair < ice->c_pairs_top && pair < &ice->c_pairs[MAX_CONN_PAIRS] && *pair;
      pair++)
    {
      if ((*pair)->state == NICE_CHECK_WAITING
      /* || (*pair)->state == NICE_CHECK_SUCCEEDED */)
        {
          portnum = ntohs((uint16_t) (*pair)->remote->addr.s.ip4.sin_port);

          /**
           * @todo Memory leak here in 'stunmsg'
           * @fixme Memory leak here in 'stunmsg'
           * FIXME Memory leak here in 'stunmsg'
           */
          x_sprintf(buf,"%s%s",(*pair)->remote->username, _ufrag);

          stunmsg = get_stun_full_request2(buf,(*pair)->remote->password);
          if (!stunmsg)
              continue;

          stunmsg_len = sizeof(stun_hdr_t) + ntohs(stunmsg->mlen);

          sock = (portnum & 0x1) ? ice->sockv4_ctl : ice->sockv4;

          TRACE("Sending STUN request > port=%d\n",
                ntohs( (uint16_t)(*pair)->remote->addr.s.ip4.sin_port));

          __gglectl_msg_tx(sock, &(*pair)->remote->addr.s.addr,
                           sizeof(struct sockaddr_in), &(*pair)->local->addr.s.addr,
                           sizeof(struct sockaddr_in), 0, (char *) stunmsg, stunmsg_len);

          (*pair)->state = NICE_CHECK_IN_PROGRESS;

          // FIXME This request should be freed
          //          stun_pkt_free(stunmsg);

          x_free(stunmsg);

        }
    }

#ifndef CS_DISABLED
//  CS_UNLOCK(CS2CS(ice->_c_pairs_lock));
#endif

}

static void
__icestun_resend_stun(x_io_stream *ice)
{
#if 1
  int sock;
  uint16_t portnum;
  size_t stunmsg_len;
  struct sockaddr_in stunsrv_addr;
  struct x_candidate_pair **pair;
  stun_hdr_t *stunmsg = NULL;

  /* fake stun server address */
  stunsrv_addr.sin_family = AF_INET;
  inet_aton("216.143.130.36", &stunsrv_addr.sin_addr);
  stunsrv_addr.sin_port = htons(3478);

  /**
   * @todo Memory leak here in 'stunmsg'
   * @fixme Memory leak here in 'stunmsg'
   * FIXME Memory leak here in 'stunmsg'
   */
  stunmsg = NULL;
  stunmsg = get_stun_simple_request();
  BUG_ON(!stunmsg);

#ifndef CS_DISABLED
//  CS_LOCK(CS2CS(ice->_c_pairs_lock));
#endif

  for (pair = &ice->c_pairs[0];
      pair < ice->c_pairs_top && pair < &ice->c_pairs[MAX_CONN_PAIRS] && *pair;
      pair++)
    {
      stunmsg_len = sizeof(stun_hdr_t) + ntohs(stunmsg->mlen);

      TRACE("Sending STUN server request\n");

      __gglectl_msg_tx(ice->sockv4_ctl, &stunsrv_addr,
          sizeof(struct sockaddr_in), &(*pair)->local->addr.s.addr,
          sizeof(struct sockaddr_in), 0, (char *) stunmsg, stunmsg_len);

      __gglectl_msg_tx(ice->sockv4, &stunsrv_addr, sizeof(struct sockaddr_in),
          &(*pair)->local->addr.s.addr, sizeof(struct sockaddr_in), 0,
          (char *) stunmsg, stunmsg_len);

    }

#ifndef CS_DISABLED
//  CS_LOCK(CS2CS(ice->_c_pairs_lock));
#endif

  // FIXME This request should be freed
  x_free(stunmsg);

#endif
}

static void *
__gglectl_Periodic(xevent_dom_t *loop, struct ev_io *data, int mask)
{
    x_io_stream *ice;
    ENTER;
    ice = (x_io_stream *) (((char *) data)
                           - offsetof(x_io_stream, iowatcher_timer));

    if (mask & EV_TIMEOUT)
    {
        //      TRACE("PERIODIC ICE EVENT!!!\n");
        __icestun_resend_candidates(ice);
        __icestun_resend_stun(ice);

        /* increase period */
        if(++ice->timer_counter > ICE_STUN_RELAXATION_COUNT)
        {
            ice->timer_counter = 0;
            ice->iowatcher_timer.repeat = ice->iowatcher_timer.repeat * 2;
        }
        ev_timer_again(ice->eventloop, &ice->iowatcher_timer);
    }

    //  EXIT;
    return 0;
}

static void
__gglectl_on_channel_ipv4_event(xevent_dom_t *loop, struct ev_io *data,
    int mask)
{
  int err = 0;
  struct sockaddr_in saddr;
  socklen_t addrlen = sizeof(saddr);
  x_io_stream *ice;
  //  ENTER;
  ice =
      (x_io_stream *) (((char *) data) - offsetof(x_io_stream, iowatcher_ipv4));
  if (mask & EV_READ)
    {
      err = __gglectl_recvmsg(X_OBJECT(ice), ice->sockv4, 0,
          (struct sockaddr *) &saddr, &addrlen);

      TRACE("Received packet from %s\n", inet_ntoa(saddr.sin_addr));
    }
  //  EXIT;
  return;
}

static void
__gglectl_on_channel_ipv4_ctl_event(xevent_dom_t *loop, struct ev_io *data,
    int mask)
{
  int err;
  struct sockaddr_in saddr;
  socklen_t addrlen = sizeof(saddr);
  x_io_stream *ice;
  //  ENTER;
  ice = (x_io_stream *) (((char *) data)
      - offsetof(x_io_stream, iowatcher_ipv4_ctl));

  if (mask & EV_READ)
    {
      err = __gglectl_recvmsg(X_OBJECT(ice), ice->sockv4_ctl, 0,
          (struct sockaddr *) &saddr, &addrlen);

      TRACE("Received control packet from %s\n", inet_ntoa(saddr.sin_addr));

    }

  //  EXIT;
}

/**
 * @todo This function __gglectl_send_msg() must be
 * placed in global xwlib api.
 */
static int
__gglectl_msg_tx(int sockfd, struct sockaddr *to, socklen_t tolen,
    struct sockaddr *from, socklen_t fromlen, int flags, char *data,
    size_t datasiz)
{
  int err = -1;
  x_iobuf_t iov[1];
  _gobee_iomsg_t mh;
#ifdef IP_PKTINFO
  char cmsbuf[CMSG_LEN(sizeof(struct in_pktinfo))];
#else /* IP_PKTINFO */
  char cmsbuf[CMSG_LEN(0)];
#endif /* IP_PKTINFO */

  struct in_pktinfo *pktinfo;
  struct cmsghdr *cmsg = (struct cmsghdr *) &cmsbuf[0];

  //  ENTER;

  X_IOV_DATA(&iov[0]) = data;
  X_IOV_LEN(&iov[0]) = datasiz;

  _gobee_iomsg_name(&mh) = to;
  _gobee_iomsg_namelen(&mh) = tolen;
  _gobee_iomsg_iov(&mh) = (x_iobuf_t *) iov;
  _gobee_iomsg_iovlen(&mh) = 1;
  _gobee_iomsg_control(&mh) = (char *) cmsg;
#ifdef IP_PKTINFO
  _gobee_iomsg_controllen(&mh) = CMSG_LEN(sizeof(struct in_pktinfo));
#else
  _gobee_iomsg_controllen(&mh) = CMSG_LEN(0);
#endif

  x_memset(&cmsbuf[0], 0, sizeof(cmsbuf));

#ifdef IP_PKTINFO
  cmsg->cmsg_level = IPPROTO_IP;
  cmsg->cmsg_type = IP_PKTINFO;
  cmsg->cmsg_len = _gobee_iomsg_controllen(&mh);
#ifdef WIN32
#else
  pktinfo = (struct in_pktinfo *) CMSG_DATA(cmsg);
  pktinfo->ipi_spec_dst.s_addr = ((struct sockaddr_in *) from)->sin_addr.s_addr;
#endif /* WIN32 s*/
#endif

  TRACE("Sending to %s:%d\n",
      inet_ntoa(((struct sockaddr_in *)to)->sin_addr),
        ntohs(((struct sockaddr_in *)to)->sin_port));

//  TRACE("Sending as %s:%d\n",
//      inet_ntoa(((struct sockaddr_in *)from)->sin_addr), ntohs(((struct sockaddr_in *)from)->sin_port));

  /* send message */
  err = x_send_msg(sockfd, &mh, flags);

#if 1
  if (err < 0)
    {
      perror("ERROR Sending data:");
    }
  else
    TRACE("Ok! Sent packet of size %d\n", err);
#endif

  //  EXIT;
  return (err);
}

/**
 * @todo This function __gglectl_send_msg() must be
 * placed in global xwlib api.
 */
static int
__gglectl_msg_vtx(int sockfd, struct sockaddr *to, socklen_t tolen,
    struct sockaddr *from, socklen_t fromlen, int flags, x_iobuf_t *iov,
    int iovlen)
{

  //  unsigned char addrbuf[sizeof(struct in6_addr)];
  char str[INET6_ADDRSTRLEN];
  struct in_pktinfo *pktinfo;

  int err = -1;
  _gobee_iomsg_t mh =
    { };
#ifdef IP_PKTINFO
  char cmsbuf[CMSG_LEN(sizeof(struct in_pktinfo))];
#else /* IP_PKTINFO */
  char cmsbuf[CMSG_LEN(0)];
#endif /* IP_PKTINFO */

  struct cmsghdr *cmsg = (struct cmsghdr *) &cmsbuf[0];

  if (to->sa_family == AF_INET)
    inet_ntop(AF_INET, &((struct sockaddr_in *) to)->sin_addr.s_addr, str,
        INET_ADDRSTRLEN);
  else
    inet_ntop(AF_INET6, &((struct sockaddr_in6 *) to)->sin6_addr, str,
        INET6_ADDRSTRLEN);

  TRACE("%s:%d\n", str, ntohs(*(uint16_t *)(void *)&to->sa_data[0]));

  _gobee_iomsg_name(&mh) = to;
  _gobee_iomsg_namelen(&mh) = tolen;
  _gobee_iomsg_iov(&mh) = (x_iobuf_t *) iov;
  _gobee_iomsg_iovlen(&mh) = iovlen;

  TRACE("BUFCOUNT=%d, buflen[1]=%d,buflen[2]=%d\n",
      iovlen, iov[0].iov_len, iov[1].iov_len);

#if 1
  _gobee_iomsg_control(&mh) = (char *) cmsg;
#ifdef IP_PKTINFO
  _gobee_iomsg_controllen(&mh) = CMSG_LEN(sizeof(struct in_pktinfo));
#else
  _gobee_iomsg_controllen(&mh) = CMSG_LEN(0);
#endif
  x_memset(&cmsbuf[0], 0, sizeof(cmsbuf));

#ifdef IP_PKTINFO
  cmsg->cmsg_level = IPPROTO_IP;
  cmsg->cmsg_type = IP_PKTINFO;
  cmsg->cmsg_len = _gobee_iomsg_controllen(&mh);
#ifdef WIN32
#else
  pktinfo = (struct in_pktinfo *) CMSG_DATA(cmsg);
  pktinfo->ipi_spec_dst.s_addr = ((struct sockaddr_in *) from)->sin_addr.s_addr;
#endif /* WIN32 s*/
#endif
#endif

  /* send message */
  err = x_send_msg(sockfd, &mh, flags);
  if (err < 0)
    perror("Send error\n");
  //  EXIT;
  return (err);
}

static void
__gglectl_iostream_init(x_io_stream *ice)
{
  static int loopcounter = 0;
  char sbuf[256];
  int opt = 1;

  ENTER;

  loopcounter++;

  /* setup networking */
    {
      ice->sockv4 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      ice->sockv4_ctl = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if ((ice->sockv4 <= 0 || ice->sockv4_ctl <= 0))
        {
//          x_object_print_path(X_OBJECT(ice)->bus, 0);
          strerror_r(errno, sbuf, sizeof(sbuf) - 1);
          TRACE("Network unavailable, LOOPS_COUNT(%d), %s\n",
              loopcounter, sbuf);
          return;
        }
      ice->sockv6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

      /* set non-blocking io mode */
      set_sock_to_non_blocking(ice->sockv4);
      set_sock_to_non_blocking(ice->sockv4_ctl);
      set_sock_to_non_blocking(ice->sockv6);

#ifdef IP_PKTINFO
      setsockopt(ice->sockv4, IPPROTO_IP, IP_PKTINFO, &opt, sizeof(int));
      setsockopt(ice->sockv4_ctl, IPPROTO_IP, IP_PKTINFO, &opt, sizeof(int));
      setsockopt(ice->sockv6, IPPROTO_IP, IP_PKTINFO, &opt, sizeof(int));
#endif
    }
  EXIT;
}


static inline int
__gglectl_get_next_rand_portnum(int *seed, int max_step) {
    *seed = *seed * 1103515245 + 12345;
    return((unsigned)(*seed/65536) % max_step);
}

static int
__gglectl_iostream_start(x_io_stream *ice)
{
  int i;
  int err;
  struct sockaddr __mysrvaddr;
  int seed = (int)ice;
  ENTER;

  /* bind to odd port for rtp payload */
  ((struct sockaddr_in *) &__mysrvaddr)->sin_family = AF_INET;
  ((struct sockaddr_in *) &__mysrvaddr)->sin_addr.s_addr = INADDR_ANY;

  for (i = 0; i < BASE_RTP_PORT_RETRY; i++)
    {
      ice->data_port = current_rtp_portnum++;
      ice->ctl_port = current_rtp_portnum++;

      ((struct sockaddr_in *) &__mysrvaddr)->sin_port = htons(ice->data_port);

      err = bind(ice->sockv4, &__mysrvaddr, sizeof(struct sockaddr));
      if (err)
        {
          TRACE("ERROR! Bind failed\n");
          /* increment base port number by random */
          current_rtp_portnum += __gglectl_get_next_rand_portnum(&seed,512);
          continue;
        }
      else
      {
          TRACE("ERROR! Bind port(%d)\n",ice->data_port);
        break;
       }
    }

  if (err)
    {
      TRACE("ERROR! Bind failed\n");
      BUG_ON(err);
      return -1;
    }
  /* bind to even port for rtcp payload */
  ((struct sockaddr_in *) &__mysrvaddr)->sin_port = htons(ice->ctl_port);

  err = bind(ice->sockv4_ctl, &__mysrvaddr, sizeof(struct sockaddr));

  TRACE("OK! port usage: %d, %d\n", ice->data_port, ice->ctl_port);

  TRACE("Connecting to stun server '%s'\n",
      (char *)_ENV((x_object *)ice,"stunserver"));

  /* add media data transport listener */
  ev_io_init(&ice->iowatcher_ipv4, &__gglectl_on_channel_ipv4_event, ice->sockv4,
      EV_READ /* | EV_WRITE */);

  /* add control data transport listener */
  ev_io_init(&ice->iowatcher_ipv4_ctl, &__gglectl_on_channel_ipv4_ctl_event,
      ice->sockv4_ctl, EV_READ /* | EV_WRITE */);

  /* add periodic listener */
  ev_timer_init(&ice->iowatcher_timer, &__gglectl_Periodic, 0., .5);

  EXIT;

  return err;
}

/**
 * Collects local candidates from '/__proc/$networking' direntry
 */
static void
__gglectl_collect_local_candidates(/* x_io_stream * */void *_ice)
{
  /** @todo Remove this */
  static int thno = 0; // thread counter (debugging)
  int i = 0;
  char portbuf[32];
  char portbuf2[32];
  x_io_stream *ice = (x_io_stream *) _ice;
  x_object *tmp, *netentry;
  x_object *ifentry;

  ENTER;

  /** @todo this must be used through x_object ref
   * get to fire release callback */
  _REFGET(X_OBJECT(ice));

  x_snprintf(portbuf, (sizeof portbuf) - 1, "%d", ice->data_port);
  x_snprintf(portbuf2, (sizeof portbuf2) - 1, "%d", ice->ctl_port);

  TRACE("Port usage: data(%d), ctl(%d)\n", ice->data_port, ice->ctl_port);

#if 0
  netentry = x_object_from_path(X_OBJECT(ice)->bus, "__proc/$networking");
#else
  netentry = _CHLD(X_OBJECT(ice)->bus,_XS("__proc"));
  BUG_ON(!netentry);
  netentry = _CHLD(netentry,_XS("$networking"));
  BUG_ON(!netentry);
#endif

  x_object_print_path(netentry,0);
  for (ifentry = _CHLD(netentry,_XS("$interface")); ifentry; ifentry =
      _NXT(ifentry), ++i)
    {
      void *src = NULL;

      /* dataport candidate */
      tmp = _GNEW(_XS("$message"),NULL);
      BUG_ON(!tmp);

      TRACE("ADDING CANDIDATE TO '%s' CHANNEL\n", _ENV(ice,"mtype"));

      _SETNM(tmp, _XS("local-candidate"));
      _ASET(tmp, _XS("address"), _AGET(ifentry,_XS("ip")));
      _ASET(tmp, _XS("port"), portbuf);
      _ASET(tmp, _XS("type"), _XS("local"));
      _ASET(tmp, _XS("protocol"), _XS("udp"));
//      _ASET(tmp, _XS("component"), _XS("1"));
      _ASET(tmp, _XS("generation"), _XS("0"));
//      _ASET(tmp, _XS("foundation"), _XS("0"));
      _ASET(tmp, _XS("network"), _XS("wlan0"));
//      _ASET(tmp, _XS("priority"), _XS("2130705663"));
      _ASET(tmp, _XS("preference"), _XS("0.9"));
      _ASET(tmp, _XS("name"), _XS("rtp"));

      _ASET(tmp, _XS("username"), ice->ownufrag);
      _ASET(tmp, _XS("password"), ice->ownpwd);

      _INS(X_OBJECT(ice), tmp);

      /* ctl port candidate */
      tmp = _CPY(tmp);
      _ASET(tmp, X_STRING("port"), portbuf2);
//      _ASET(tmp, X_STRING("component"), _XS("2"));
      _ASET(tmp, _XS("name"), _XS("rtcp"));

      _INS(X_OBJECT(ice), tmp);
    }

  TRACE("Added %d local candidates\n",i);

  _REFPUT(X_OBJECT(ice), NULL);

  EXIT;
}

static void
__icectl_idle_cb  (xevent_dom_t *loop, struct ev_idle *w, int revents)
{
//    sched_yield();
    usleep(100);
}

/**
 * @todo gglectl should be inside __iostream (move it there)
 * Thread entry
 */
static void *
__gglectl_worker(void *ctx)
{
    struct ev_idle idle_watcher;
  int cand_tout = 3;
  x_io_stream *ice = (x_io_stream *) (void *) ctx;

  ENTER;

#if 0 /* Changed 12.10.2012 */
  TRACE("Waiting on start condition...\n");
#   ifndef CS_DISABLED
     CS_LOCK(CS2CS(ice->_worker_lock));
#   endif
#endif

     ev_idle_init (&idle_watcher, &__icectl_idle_cb);
     ev_idle_start (ice->eventloop, &idle_watcher);

  TRACE("Started new thread\n");

#ifndef __DISABLE_MULTITHREAD__
  do
    {
//      ev_loop(ice->eventloop, 0);
      ev_loop(ice->eventloop, EVLOOP_NONBLOCK);
    }
  while ((ice->regs.cr0 & (1 << 1)) /* thread stop condition!!! */);
#endif

#if 0 /* Changed 12.10.2012 */
#   ifndef CS_DISABLED
     CS_UNLOCK(CS2CS(ice->_worker_lock));
#   endif
#endif


  EXIT;
  return NULL;
}

static int
__gglectl_worker_start(x_io_stream *ice)
{
  int err;

  ENTER;

  TRACE("\n");
  ice->eventloop = ev_loop_new(EVFLAG_AUTO);
  BUG_ON(!ice->eventloop);
  ev_io_start(ice->eventloop, &ice->iowatcher_ipv4);
  ev_io_start(ice->eventloop, &ice->iowatcher_ipv4_ctl);
  ev_timer_start(ice->eventloop, &ice->iowatcher_timer);

  TRACE("\n");
  if(x_thread_run(&ice->input_tid, &__gglectl_worker, (void *) ice) == 0)
      ice->regs.cr0 |= (1 << 1); // started state

  _ws_exit:
  EXIT;
  return err;
}

static int
__gglectl_worker_stop(x_io_stream *ice)
{
  /* cancel working thread */
  ice->regs.cr0 &= ~((uint32_t) (1 << 1));

#if 0 /* Changed 12.10.2012 */
#   ifndef CS_DISABLED
      CS_LOCK(CS2CS(ice->_worker_lock));
      CS_UNLOCK(CS2CS(ice->_worker_lock));
#   endif
#endif

  ev_io_stop(ice->eventloop, &ice->iowatcher_ipv4);
  ev_io_stop(ice->eventloop, &ice->iowatcher_ipv4_ctl);
  ev_timer_stop(ice->eventloop, &ice->iowatcher_timer);

  pthread_join(ice->input_tid,NULL);
}

static int
__gglectl_insert_local_candidate(x_io_stream *ice, x_candidate *cand)
{
  x_candidate **remote;
  struct x_candidate_pair *pair;

  int err = 0;
  ENTER;

  TRACE("%d\n", cand->stream_id);

  /** @todo CS_LOCK() should check or return error */
  if (ice->locals_top >= &ice->locals[MAX_CONN_PAIRS])
    goto ilc_end;

  *ice->locals_top++ = cand;

  for (remote = &ice->remotes[0];
      remote < ice->remotes_top && remote < ice->remotes[MAX_CONN_PAIRS]
          && *remote; remote++)
    {
      pair = x_malloc(sizeof(struct x_candidate_pair));
      x_memset(pair, 0, sizeof(struct x_candidate_pair));
      pair->local = cand;
      pair->remote = *remote;
      pair->stream_id = random();
      pair->state = NICE_CHECK_WAITING;

      /* push new pair into pairs stack */
//#ifndef CS_DISABLED
//      CS_LOCK(CS2CS(ice->_c_pairs_lock));
//#endif
      *ice->c_pairs_top++ = pair;
//#ifndef CS_DISABLED
//      CS_UNLOCK(CS2CS(ice->_c_pairs_lock));
//#endif
    }

  ilc_end:

  EXIT;
  return err;
}

static int
__gglectl_insert_remote_candidate(x_io_stream *ice, x_candidate *remote)
{
  x_candidate **local;
  struct x_candidate_pair *pair;

  int err = 0;
  ENTER;

  /** @todo CS_LOCK() should check or return error */
  /** @todo this should be done in lock free manner (see atomics) */
  if (ice->remotes_top >= &ice->remotes[MAX_CONN_PAIRS])
    goto irc_end;
  *ice->remotes_top++ = remote;

  TRACE("%d\n", remote->stream_id);

  for (local = &ice->locals[0];
      local < ice->locals_top && local < &ice->locals[MAX_CONN_PAIRS] && *local;
      local++)
    {
      pair = x_malloc(sizeof(struct x_candidate_pair));
      x_memset(pair, 0, sizeof(struct x_candidate_pair));
      pair->remote = remote;
      pair->local = *local;
      pair->stream_id = random();
      pair->state = NICE_CHECK_WAITING;
#ifndef CS_DISABLED
      CS_LOCK(CS2CS(ice->_c_pairs_lock));
#endif
      *ice->c_pairs_top++ = pair;
#ifndef CS_DISABLED
      CS_UNLOCK(CS2CS(ice->_c_pairs_lock));
#endif
    }

  irc_end:

  EXIT;
  return err;
}

static void
gglectl_on_create(x_object *o)
{
  char _ufrag[33];
  char _pwd[48];
  x_io_stream *ice = (x_io_stream *) (void *) o;
  ENTER;

//  memset(ice,0,sizeof(x_io_stream));

  TRACE("\n");

  ice->request = 0;
  ice->c_pairs_top = &ice->c_pairs[0];
  ice->locals_top = &ice->locals[0];
  ice->remotes_top = &ice->remotes[0];
  ice->regs.cr0 = 0;

  x_memset(ice->media_owner_cache,0,sizeof(ice->media_owner_cache));

#ifndef CS_DISABLED
#if 0 /* Changed 12.10.2012 */
  CS_INIT(CS2CS(ice->_worker_lock));
  CS_LOCK(CS2CS(ice->_worker_lock));
#endif
  CS_INIT_ERRCHK(CS2CS(ice->_c_pairs_lock));
#endif

  /* we initialize pwd and ufrag here,
   * not in 'append' call because
   * of we need to be ready before appending to parent
   */

  x_snprintf(_ufrag, sizeof(_ufrag) - 1, "una%08d", (long long)random());
  x_snprintf(_pwd, sizeof(_pwd) - 1, "ice%08d", (long long)random());

  _ASET(o, "xmlns", "http://www.google.com/transport/p2p");
  ice->ownpwd = x_strdup(_pwd);
  ice->ownufrag = x_strdup(_ufrag);

  EXIT;
}

static int
gglectl_on_append(x_object *o, x_object *parent)
{
  int err = 0;
  int pcapfd;
  x_object *xobj;
  x_io_stream *ice = (x_io_stream *) (void *) o;
  pcap_hdr_t phdr =
    { 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1 };
  ENTER;

  TRACE("\n");

  // set transport object ready
  _CRSET(o,1);

#if 0 /* Changed 12.10.2012 */
  /**
    * FIXME This MUST be called when session activated (see x_session_start())
    * @todo This MUST be called when session activated (see x_session_start())
    */
  __gglectl_worker_start(ice);
#endif

  __gglectl_iostream_init(ice);
  err = __gglectl_iostream_start(ice);
  if (err)
    {
      TRACE("ERROR\n");
      return -1;
    }

  // collect local transport candidates
  __gglectl_collect_local_candidates(ice);

  TRACE("\n");
#if 1
  if (!(ice->regs.cr0 & (1 << 1)) /* worker not started */
    /* && ice->otherufrag && ice->otherpwd */)
    {
      TRACE("\n");

#if 0 /* Changed 12.10.2012 */
#   ifndef CS_DISABLED
      CS_UNLOCK(CS2CS(ice->_worker_lock));
#   endif
#else
    __gglectl_worker_start(ice);
#endif

    }
#endif

  /*
   _ASET(o,_XS("stunserver"),_XS("stun.sipgate.net"));
   _ASET(o,_XS("stunport"),_XS("stun.sipgate.net"));
   xobj = x_object_new_ns(X_STRING("__stunserver"), X_STRING("jingle"));
   x_object_append_child(o, xobj);
   */

#ifdef DEBUG
  pcapfd = open("sender.pcap", O_RDWR | O_TRUNC | O_CREAT,
      S_IRWXU | S_IRWXG | S_IRWXO);
  write(pcapfd,(void*)&phdr,sizeof(phdr));
  close(pcapfd);
#endif

  EXIT;
  return err;
}

static void UNUSED
gglectl_remove_cb(x_object *o)
{
  x_io_stream *ice = (x_io_stream *) (void *) o;
  ENTER;
  __gglectl_worker_stop(ice);

  if(ice->ownpwd)
      x_free(ice->ownpwd);

  if(ice->ownufrag)
      x_free(ice->ownufrag);

  EXIT;
}

/**
 * This checks for appended childs and
 * their types.
 */
static void
gglectl_on_child_append(x_object *o, x_object *child)
{
  int err;
  char buf[32];
  const char *tmpstr;
  x_candidate *cand;
  x_io_stream *ice = (x_io_stream *) (void *) o;
  x_obj_attr_t hints =
    { 0, 0, 0 };
  x_object *msg;
  x_object *body;
  //  ENTER;

  // x_object_print_path(child, 0);
  //  attr_list_init(&hints);

  if (EQ("local-candidate",_GETNM(child)))
    {
      cand = x_malloc(sizeof(x_candidate));
      err = __icestun_xobj_to_sockaddr_in(child, &cand->addr.s.ip4);

      if (!err)
        {
            TRACE("INSERTING local cand\n");
          __gglectl_insert_local_candidate(ice, cand);

          TRACE("\n");

          cand->stream_id = random();
          x_snprintf(buf, sizeof(buf) - 1, "%d", cand->stream_id);
          /* Set candidate id */
//          _ASET(child, _XS("id"), buf);

          msg = _GNEW(_XS("$message"),NULL);
          _ASET(msg, _XS("subject"), _XS("candidate-new"));

          TRACE("\n");

          body = _CPY(child);

          _SETNM(body, "candidate");
          _INS(msg, body);

//          if (EQ(_AGET(child,"type"),"srflx"))
          x_object_send_down(_PARNT(o), msg, &hints);

          _REFPUT(msg,NULL);

          TRACE("\n");
        }
    }
  else if (EQ("candidate",_GETNM(child)))
    {
      cand = x_malloc(sizeof(x_candidate));
      err = __icestun_xobj_to_sockaddr_in(child, &cand->addr.s.ip4);
      if (!err)
        {
          if ((tmpstr = _AGET(child,_XS("username"))))
            {
              cand->username = x_strdup(tmpstr);
            }
          else if ((tmpstr = _ENV(child,_XS("ufrag"))))
            {
              cand->username = x_strdup(tmpstr);
            }

          if ((tmpstr = _AGET(child,_XS("password"))))
            {
              cand->password = x_strdup(tmpstr);
            }
          else if ((tmpstr = _ENV(child,_XS("pwd"))))
            {
              cand->password = x_strdup(tmpstr);
            }

          TRACE("INSERTING remote cand: '%p':'%p', type(%s)\n",
              cand->username, cand->password, _AGET(child,_XS("type")));

          __gglectl_insert_remote_candidate(ice, cand);

        }
    }

  //  EXIT;
}

static void UNUSED
gglectl_release_cb(x_object *o)
{
  ENTER;
  EXIT;
}

/* matching function */
static BOOL
gglectl_equals(x_object *o, x_obj_attr_t *attrs)
{
  return TRUE;
}

static int
gglectl_classmatch( x_object *to UNUSED,  x_obj_attr_t *attr UNUSED)
{
  ENTER;
  EXIT;
  return 1;
}

static int
gglectl_try_writev(x_object *o, const struct iovec *iov, int iovcnt,
    x_obj_attr_t *attr)
{
  const char *typ = NULL;
  BOOL isctl = FALSE;
  x_io_stream *ice = (x_io_stream *) (void *) o;
  int sockfd = ice->sockv4;

  ENTER;

  if (!(ice->xobj.cr & ICE_FLAG_RTP_SUCCEEDED))
    {
      TRACE("Transport not ready!!!\n");
      return -1;
    }

  if (iovcnt < 3)
    {
      EXIT;
      return -1;
    }

  if (attr)
    {
      typ = getattr(X_STRING("type"), attr);
      if (typ)
        {
          isctl = (BOOL) EQ(typ,X_STRING("control"));
          if (isctl)
            sockfd = ice->sockv4_ctl;
        }
    }

  __gglectl_msg_tx(sockfd, (struct sockaddr *) (void *) iov[0].iov_base,
      iov[0].iov_len, (struct sockaddr *) (void *) iov[1].iov_base,
      iov[1].iov_len, 0, (void *) iov[2].iov_base, iov[2].iov_len);

  EXIT;
  return 1;
}

static int
gglectl_try_write(x_object *o, void *buf, int len, x_obj_attr_t *attr)
{
  int err = -1;
  struct x_candidate_pair **pair;
  const char *typ = NULL;
  BOOL isctl = FALSE;
  x_io_stream *ice = (x_io_stream *) (void *) o;
  struct sockaddr *raddr = NULL; /* remote address (destination) */
  struct sockaddr *laddr = NULL; /* our local address (source) */
  struct sockaddr_in _raddr;
  struct sockaddr_in _laddr;
  int sockfd = ice->sockv4;
  int ssrc;

//  if (!buf || len <= 0)
//    {
//      EXIT;
//      return -1;
//    }

  if (!(ice->xobj.cr & ICE_FLAG_RTP_SUCCEEDED))
    {
      TRACE("Transport not ready!!!\n");
      return -1;
    }

  /**
   * @todo This attribute checking peace is slow > redesign this
   */
  if (attr)
    {
      typ = getattr(X_STRING("type"), attr);
      if (typ)
        {
          isctl = (BOOL) EQ(typ,X_STRING("control"));
          if (isctl)
            sockfd = ice->sockv4_ctl;
        }
    }

  /**
   * Find first validated transmit pair and send to it
   *
   * @todo Fixme: get pair function should just take pair from top
   * of pairs stack, which should be sorted each time pushed
   * with a new pair.
   */
  if (!ice->c_pair_last_success)
    {
      for (pair = &ice->c_pairs[0];
          pair < ice->c_pairs_top && pair < &ice->c_pairs[MAX_CONN_PAIRS]
              && *pair; pair++)
        {
          if ((*pair)->state == NICE_CHECK_SUCCEEDED
              && !(ntohs((*pair)->remote->addr.s.ip4.sin_port) & 1))
            {
              laddr = &(*pair)->local->addr.s.addr;
              raddr = &(*pair)->remote->addr.s.addr;
              if (raddr->sa_family == AF_INET)
                if (((struct sockaddr_in *) raddr)->sin_addr.s_addr)
                  {
                    if (!ice->c_pair_last_success)
                        ice->c_pair_last_success = *pair;
                    break;
                  }
            }
        }
    }

  if (ice->c_pair_last_success)
    {
      laddr = &ice->c_pair_last_success->local->addr.s.addr;
      raddr = &ice->c_pair_last_success->remote->addr.s.addr;
      ssrc = ice->c_pair_last_success->local->stream_id;
    }

  if (raddr && laddr)
    {
      const char *tstr;
      x_iobuf_t iov[2];
      rtp_hdr_t rtp;

      pcaprec_hdr_t prec;
      int pcapfd;
      char *__buf;
      int __len;

      X_IOV_DATA(&iov[0]) = &rtp;
      X_IOV_LEN(&iov[0]) = sizeof(rtp_hdr_t);

      X_IOV_DATA(&iov[1]) = buf;
      X_IOV_LEN(&iov[1]) = len;

      rtp.octet1.control1.v = 2;
      rtp.octet1.control1.p = 0;
      rtp.octet1.control1.x = 0;
      rtp.octet1.control1.cc = 0;
      rtp.octet2.control2.m = 0;

      tstr = getattr("pt", attr);
      rtp.octet2.control2.pt = atoi(tstr) & 0x7f;

      tstr = getattr("timestamp", attr);
      TRACE(" stamp=%s\n", tstr);
      if (tstr)
        {
          rtp.t_stamp = htonl(atoi(tstr));
        }

      rtp.ssrc_id = htonl(ssrc);

      tstr = getattr("seqno", attr);
      TRACE(" seqno=%s\n", tstr);
      if (tstr)
        {
          rtp.seq = htons((uint16_t) (atoi(tstr) & 0xffff));
        }

      /* here we receive raw data. we cannot do
       * packet fragmentation at this level because of
       * upper levels may include fragmentation info in
       * their payload.
       */
#if 1

      /**
       * use following command to decode diagnostic stream
       * @code
       * $ gst-launch-0.10  udpsrc port=24444 caps=application/x-rtp,media=video,payload=96,clock-rate=90000,encoding-name=THEORA
       *        ! queue ! rtptheoradepay ! theoradec ! ffmpegcolorspace ! videoscale ! ximagesink
       * @endcode
       */
//#define DIAGNOSE
#ifndef DIAGNOSE
      err = __gglectl_msg_vtx(sockfd, raddr, sizeof(struct sockaddr_in), laddr,
          sizeof(struct sockaddr), 0, &iov, 2);

#else // duplicate packet for diagnostic
      if (EQ(_ENV(o,"mtype"),"video"))
        {
          _laddr.sin_family = AF_INET;
          _raddr.sin_family = AF_INET;

          inet_aton("127.0.0.1", &_laddr.sin_addr);
          _laddr.sin_port = htons(24102);

          //          _raddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
          inet_aton("127.0.0.1", &_raddr.sin_addr);
          _raddr.sin_port = htons(24444);

          err = __gglectl_msg_vtx(sockfd, &_raddr, sizeof(struct sockaddr_in),
              &_laddr, sizeof(struct sockaddr), 0, &iov, 2);
        }
#endif // DIAGNOSE
#else
      __len = sizeof(rtp_hdr_t) + len;
      __buf = malloc(__len);
      memcpy(&__buf[0], &rtp, sizeof(rtp));
      memcpy(&__buf[sizeof(rtp)], &buf[0], len);
      err = sendto(sockfd, &__buf[0], __len, 0, raddr, sizeof(struct sockaddr));
      free(__buf);
#endif

      TRACE("Sent %d bytes\n", err);

#if 0
        {
          pcapfd = open("sender.pcap", O_WRONLY | O_APPEND);

          prec.ts_sec = time(NULL);
          prec.ts_usec = 0;
          prec.incl_len = iov[0].iov_len + iov[1].iov_len;
          prec.orig_len = iov[0].iov_len + iov[1].iov_len;

          write(pcapfd,(void*)&prec,sizeof(prec));

          write(pcapfd,iov[0].iov_base,iov[0].iov_len);
          write(pcapfd,iov[1].iov_base,iov[1].iov_len);

          close(pcapfd);
        }
#endif

    }
  else
    {
      TRACE("Cannot find valid address to send!!!\n");
    }

  return err;
}

static int UNUSED
gglectl_tx(x_object *to, x_object *o, x_obj_attr_t *ctx_attrs)
{
  int err;
  x_object *msg;
  ENTER;

  msg = _CPY(to);
  BUG_ON(!msg);

  TRACE("Received object %p <- %p, sending %p\n", to, o, msg);

  _INS(msg, o);

  err = x_object_send_down(x_object_get_parent(to), msg, NULL);
  _REFPUT(msg,NULL);

  EXIT;
  return err;
}

static x_object *
gglectl_assign(x_object *o, x_obj_attr_t *attr)
{
  const char *lufrag;
  const char *ufrag;
  const char *pwd;
  x_io_stream *ice = (x_io_stream *) (void *) o;

  ENTER;

//  ufrag = getattr(_XS("ufrag"), attr);
//  pwd = getattr(_XS("pwd"), attr);
//  lufrag = _ENV(X_OBJECT(ice),_XS("ufrag"));

//  TRACE("%s-%s-%s\n",ufrag,pwd,lufrag);

  /**
   * @todo Fixme This duplicated in on_append() func,
   * One state transition func should be provided for
   * state checking...
   */
//  if (ufrag && !ice->otherufrag)
//    ice->otherufrag = x_strdup(ufrag);

//  if (pwd && !ice->otherpwd)
//    ice->otherpwd = x_strdup(pwd);


  /*
    start worker thread if have not yet started
    but already connected to parent
    */
  if (_PARNT(o) != NULL
          && !(ice->regs.cr0 & (1 << 1)) /* worker not started */
         /* && ice->otherufrag && ice->otherpwd */)
  {
      TRACE("\n");
#if 0 /* Changed 12.10.2012 */
#   ifndef CS_DISABLED
      CS_UNLOCK(CS2CS(ice->_worker_lock));
#   endif
#else
      __gglectl_worker_start(ice);
#endif
  }
    else
  {
      TRACE("\n");
  }
  EXIT;
  return o;
}

/*
 *
 */
static struct xobjectclass __gglectl_objclass;

__x_plugin_visibility
__plugin_init void
__gglectl_init(void)
{
  ENTER;

  TRACE("objclass(%p)\n",&__gglectl_objclass);

  __gglectl_objclass.cname = X_STRING("__icectl");
  __gglectl_objclass.psize = (unsigned int) (sizeof(x_io_stream)
      - sizeof(x_object));

  __gglectl_objclass.classmatch = &gglectl_classmatch;
  __gglectl_objclass.on_create = gglectl_on_create;
  __gglectl_objclass.on_append = gglectl_on_append;
  __gglectl_objclass.on_remove = gglectl_remove_cb;
  __gglectl_objclass.on_child_append = &gglectl_on_child_append;
  __gglectl_objclass.on_release = &gglectl_release_cb;
  __gglectl_objclass.try_writev = &gglectl_try_writev;
  __gglectl_objclass.tx = &gglectl_tx;
  __gglectl_objclass.match = &gglectl_equals;
  __gglectl_objclass.on_assign = &gglectl_assign;
  __gglectl_objclass.try_write = &gglectl_try_write;

  x_class_register_ns(__gglectl_objclass.cname,
                      &__gglectl_objclass, "http://www.google.com/transport/p2p");
  EXIT;
  return;
}

PLUGIN_INIT(__gglectl_init);
