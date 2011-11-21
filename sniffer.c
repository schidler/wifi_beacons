#include<error.h>
#include <netinet/in.h>    
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if_ether.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <string.h>
#include <stdlib.h>
#include "jigdump.h"
#include <syslog.h>
#include <linux/wireless.h>
#include <errno.h>
#include <math.h>
#include "linux_ieee80211_radiotap.h"
#include "pcap.h"
#include "packet.h"
#define JIGBLOCK_MAX_SIZE (700) 
//(16000)
#define MAX_PACKET_LEN 10240
uint8_t data[MAX_PACKET_LEN];

#define BITNO_32(x) (((x) >> 16) ? 16 + BITNO_16((x) >> 16) : BITNO_16((x)))
#define BITNO_16(x) (((x) >> 8) ? 8 + BITNO_8((x) >> 8) : BITNO_8((x)))
#define BITNO_8(x) (((x) >> 4) ? 4 + BITNO_4((x) >> 4) : BITNO_4((x)))
#define BITNO_4(x) (((x) >> 2) ? 2 + BITNO_2((x) >> 2) : BITNO_2((x)))
#define BITNO_2(x) (((x) & 2) ? 1 : 0)
#define BIT(n)  (1 << n)


static u_int ieee80211_mhz2ieee(u_int freq, u_int flags) {
  if (flags & IEEE80211_CHAN_2GHZ) {          /* 2GHz band */
    if (freq == 2484)
      return 14;
    if (freq < 2484)
      return (freq - 2407) / 5;
    else
      return 15 + ((freq - 2512) / 20);
  } else if (flags & IEEE80211_CHAN_5GHZ) {   /* 5Ghz band */
    return (freq - 5000) / 5;
  } else {                                    /* either, guess */
    if (freq == 2484)
      return 14;
    if (freq < 2484)
      return (freq - 2407) / 5;
    if (freq < 5000)
      return 15 + ((freq - 2512) / 20);
    return (freq - 5000) / 5;
  }
}


union {
  int8_t  i8;
  int16_t i16;
  u_int8_t        u8;
  u_int16_t       u16;
  u_int32_t       u32;
  u_int64_t       u64;
} u;
union {
  int8_t          i8;
  int16_t         i16;
  u_int8_t        u8;
  u_int16_t       u16;
  u_int32_t       u32;
  u_int64_t       u64;
} u2;
typedef unsigned char      uchar; 
int rcv_timeo = 600;
int set_radio_channel(const char device[], int channel)
{
	/*	int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
		struct iwreq    wrq;
		memset(&wrq, 0, sizeof(wrq));
		strncpy(wrq.ifr_name, device, IFNAMSIZ);

		struct iw_freq iwf ; *iwf = wrq.u.freq;
		double freq = 2.412*1e+9 + (channel-1)*5.0*1e+6;
		iwf.e = (short) (floor(log10(freq)));
		if(iwf.e > 8) {
		iwf.m = ((long) (floor(freq/pow(10.0, (double)(iwf.e - 6))))) * 100;
		iwf.e -= 8;
		} else {
		iwf.m = (long) freq;
		iwf.e = 0;
		}

		if (0 > ioctl(sd, SIOCSIWFREQ, &wrq)) {
		syslog(LOG_ERR, "ioctl(SIOCSIWFREQ): %s\n", strerror(errno));
		return 1;
		}
	 */
  return 0;
}


int config_radio_interface(const char device[])
{
  int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  struct iwreq    wrq;
  memset(&wrq, 0, sizeof(wrq));
  strncpy(wrq.ifr_name, device, IFNAMSIZ);
  
  wrq.u.mode = IW_MODE_MONITOR;
  if (0 > ioctl(sd, SIOCSIWMODE, &wrq)) {
    printf("ioctl(SIOCSIWMODE) : %s\n", strerror(errno));
    syslog(LOG_ERR, "ioctl(SIOCSIWMODE): %s\n", strerror(errno));
    return 1;
  }
#if  1
	/*
	   if(wrq.u.mode == IW_MODE_MONITOR){
	   printf("The device is in monitor mode \n");
	   }
	 */
	/*	wrq.u.mode = 2;//6 {6 is monitor mode}; //b/g  mode <= this is what mode Jigsaw guys used 2 for ?
		wrq.u.data.length = 3;
		wrq.u.data.flags = 0;
		if (0 > ioctl(sd, SIOCIWFIRSTPRIV, &wrq)) {
		printf("ioctl(SIOCIWFIRSTPRIV) : %s\n", strerror(errno));
		syslog(LOG_ERR, "ioctl(SIOCIWFIRSTPRIV): %s\n", strerror(errno));
		return 1;
		}
	 */
#endif
	return 0;
}

int from_pcap(int in_fd, const char device[]){

  /*  struct ifreq    ifr;
      
      This gives the hardware descriptor number.  
      memset(&ifr, 0, sizeof(ifr));
      strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
      
      if (ioctl(in_fd, SIOCGIFHWADDR, &ifr) == -1) {
      printf("SIOCGIFHWADDR err: %s", strerror(errno));
      if (errno == ENODEV) {
      printf("no such device present\n");
      }
      printf("SIOCGIFHWADDR err: somthing else\n");
      }
      
      printf("arptype =%d\n",ifr.ifr_hwaddr.sa_family); //gives back 803 !
      
  */
  /*Device Does supports wireless extension. Safe check */
  /*  struct iwreq ireq;
      strncpy(ireq.ifr_ifrn.ifrn_name, device,sizeof ireq.ifr_ifrn.ifrn_name);
      ireq.ifr_ifrn.ifrn_name[sizeof ireq.ifr_ifrn.ifrn_name - 1] = 0;
      if (ioctl(in_fd, SIOCGIWNAME, &ireq) >= 0){
      printf("device supports wireless extension\n");
      }else if(errno == ENODEV){
      printf("SIOCGIWNAME: no such device present\n"); 
      }
      memset(&ifr, 0, sizeof(ifr));
      strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
      if (ioctl(in_fd, SIOCGIFMTU, &ifr) == -1) {
      printf("SIOCGIFMTU: %s", strerror(errno));
      }else{
      printf("mtu of the kernel is %d\n",ifr.ifr_mtu); //gives 1500
      }
  */
  /* The PRIV calls are deprecated; Folloinwg code from pcap is not of use now*/
  /* int in_fd; 
     if ((in_fd = socket(PF_PACKET, SOCK_DGRAM, (htons(ETH_P_ALL)))) < 0) {
     printf("Failed to create AF_INET DGRAM socket %d:%s", errno, strerror(errno));
     return 1;
     }
     
     int args[2];
     struct iwreq wrq ;
     strncpy(wrq.ifr_ifrn.ifrn_name, device,
     sizeof wrq.ifr_ifrn.ifrn_name);
     wrq.ifr_ifrn.ifrn_name[sizeof wrq.ifr_ifrn.ifrn_name - 1] = 0;
     wrq.u.data.pointer = args;
     wrq.u.data.length = 0;
     wrq.u.data.flags = 0;
     if (ioctl(in_fd, SIOCGIWPRIV, &wrq) != -1) {
     printf(" SIOCGIWPRIV with a zero-length buffer didn't fail:%d  %s\n",errno,strerror(errno));
     }
     if (errno == EOPNOTSUPP) {
     printf("%s:SIOCGIWPRIV; device does not support private calls :%d %s\n", device,errno,strerror(errno));
     }
     if (errno != E2BIG) {
     printf("%s:SIOCGIWPRIV;if supports, it should give E2BIG  :%d %s\n", device,errno, strerror(errno));
     }
     struct iw_priv_args *priv;
     priv = malloc(wrq.u.data.length * sizeof (struct iw_priv_args));
     if (priv == NULL) {
     printf("malloc failed: %s\n", strerror(errno));
     return 1;  
     }
     wrq.u.data.pointer = priv;
     if (ioctl(in_fd, SIOCGIWPRIV, &wrq) == -1) {
     printf("%s: SIOCGIWPRIV; can't fetch data:%d, %s\n", device,errno, strerror(errno));
     free(priv);
     return 1;
     }
     int i ;
     for (i = 0; i < wrq.u.data.length; i++) {
     printf("name of interface is %s\n",priv[i].name);
     }
  */
  return 0;
}



int up_radio_interface(const char device[])
{
  int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, device, IFNAMSIZ);
  if (-1 == ioctl(sd, SIOCGIFFLAGS, &ifr)) {
    printf("ioctl(SIOCGIFFLAGS) : %s\n", strerror(errno));
    syslog(LOG_ERR, "ioctl(SIOCGIFFLAGS): %s\n", strerror(errno));
    return 1;
  }
  const int flags = IFF_UP|IFF_RUNNING|IFF_PROMISC;
  if (ifr.ifr_flags  == flags)
    return 0;
  ifr.ifr_flags = flags;
  if (-1 == ioctl(sd, SIOCSIFFLAGS, &ifr)) {
    printf("ioctl(SIOCSIFFLAGS) : %s\n", strerror(errno));
    syslog(LOG_ERR, "ioctl(SIOCSIFFLAGS): %s\n", strerror(errno));
    return 1;
  }
  
  return 0;
}

int down_radio_interface(const char device[])
{
  int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, device, IFNAMSIZ);
  if (-1 == ioctl(sd, SIOCGIFFLAGS, &ifr)) {
    printf("ioctl(SIOCGIFLAGS) : %s\n", strerror(errno));
    syslog(LOG_ERR, "ioctl(SIOCGIFFLAGS): %s\n", strerror(errno));
    return 1;
  }
  if (0 == ifr.ifr_flags)
    return 0;
  ifr.ifr_flags = 0;
  if (-1 == ioctl(sd, SIOCSIFFLAGS, &ifr)) {
    printf("ioctl(SIOCSIWMODE) : %s\n", strerror(errno));
    syslog(LOG_ERR, "ioctl(SIOCSIFFLAGS): %s\n", strerror(errno));
    return 1;
  }
  return 0;
}

int open_infd(const char device[], int skbsz)
{
  int in_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (in_fd < 0) {
    printf("socket(PF_PACKET): %s\n", strerror(errno));
    return -1;
  }
  
  struct ifreq ifr;
  strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
  
  if (0 > ioctl(in_fd, SIOCGIFINDEX, &ifr)) {
    printf("ioctl(SIOGIFINDEX): %s\n", strerror(errno));
    return -1;
  }
  //printf("the ifindex of device is %d\n",ifr.ifr_ifindex);

  struct sockaddr_ll sll;
  memset(&sll, 0, sizeof(sll));
  sll.sll_family  = AF_PACKET;
  sll.sll_ifindex = ifr.ifr_ifindex;
  sll.sll_protocol= htons(ETH_P_ALL);
  if (0 > bind(in_fd, (struct sockaddr *) &sll, sizeof(sll))) {
    printf("bind(): %s\n", strerror(errno));
    return -1;
  }
  if (0 > setsockopt(in_fd, SOL_SOCKET, SO_RCVBUF, &skbsz, sizeof(skbsz))) {
    printf("setsockopt(in_fd, SO_RCVBUF): %s\n", strerror(errno));
    return -1;
  }
  int skbsz_l = sizeof(skbsz);
  if (0 > getsockopt(in_fd, SOL_SOCKET, SO_RCVBUF, &skbsz,
		     (socklen_t*)&skbsz_l)) {
    printf("getsockopt(in_fd, SO_RCVBUF): %s\n", strerror(errno));
    return -1;
  }
  
  struct timeval rto = { rcv_timeo, 0};
  if (rcv_timeo > 0 &&
      0 > setsockopt(in_fd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto))) {
    printf( "setsockopt(in_fd, SO_RCVTIMEO): %s\n", strerror(errno));
    return -1;
  }
  return in_fd;
}

int main(int argc, char* argv[])
{
  int skbsz = 1U << 23 ;
  char* device = argv[1] ; 
  int in_fd = -1;
  printf("starting up\n");
  if (down_radio_interface(device)){
    printf("the radio interface is down \n");
    return 1;
  }
  if (up_radio_interface(device)){
    printf("the radio interface is not up \n");
    return 1;
  }
  if (config_radio_interface(device)){
    printf("the radio interface is not configured \n");
    return 1;
  }

  //	int channel =11; // 11 ;
  //	if (set_radio_channel(device, channel)){
  //		printf("the radio channel is not set proper \n");
  //		return 1;
  //	}
  
  printf("i am done with checks !\n");

  in_fd = open_infd(device, skbsz);
  if (in_fd < 0) {
    printf("can not open capture device %s\n", device);
    return -1;
  }
  //from_pcap(in_fd,device);
  
  struct sockaddr_ll      from;
  socklen_t               fromlen;
  fromlen = sizeof(from);
  
  int gh; 
  for (;;){
    //struct kis_packet *packet;
    //memset(packet, 0, sizeof(kis_packet));

    uchar jb [JIGBLOCK_MAX_SIZE];
    int length = 0; /*length of the received frame*/ 
    const int jb_sz =  sizeof(jb);
    int jb_len;	
    length = recvfrom(in_fd, jb, jb_sz, MSG_TRUNC,(struct sockaddr *) &from,&fromlen ); //NULL,NULL);
    if (length == -1) { 
      printf("null capture\n") ;
    }else if (length ==0 ){
      printf("interface down\n");
      break;
    }
    if(length> jb_sz){
      printf("block is truncated %d\n", MSG_TRUNC);
      continue ;
    }
    if (length >0) {	
      jb_len=length ;
      printf("length captured by recv%d\n",length) ; 
    }
    struct ieee80211_radiotap_header *hdr;
    u_int32_t present, next_present;
    u_int32_t *presentp, *last_presentp;
    enum ieee80211_radiotap_type bit;
    int bit0;
    const u_char *iter;
    // do we cut the FCS?  this is influenced by the radiotap headers and 
    // by the class fcsbytes value in case of forced fcs settings (like openbsd
    // atheros and Ralink USB at the moment)
    int fcs_cut = 0;

    hdr = (struct ieee80211_radiotap_header *) jb;

    // if (callback_header.caplen < sizeof(*hdr)) {
    //  printf("packet not right\n");
    //  continue ; 
    //}
    
    for (last_presentp = &hdr->it_present;
	 (EXTRACT_LE_32BITS(last_presentp) & BIT(IEEE80211_RADIOTAP_EXT)) != 0 &&
	   (u_char*)(last_presentp + 1) <= data + EXTRACT_LE_16BITS(&hdr->it_len);
	 last_presentp++);
    

    if ((EXTRACT_LE_32BITS(last_presentp) & BIT(IEEE80211_RADIOTAP_EXT)) != 0) {
      printf("packet not right\n");
      continue ;
    }
   
    //    packet->caplen = packet->len = callback_header.caplen;
    
    iter = (u_char*)(last_presentp + 1);
    
    for (bit0 = 0, presentp = &hdr->it_present; presentp <= last_presentp;
	 presentp++, bit0 += 32) {
      for (present = EXTRACT_LE_32BITS(presentp); present; present = next_present) {

	next_present = present & (present - 1);

	bit = (enum ieee80211_radiotap_type)
	  (bit0 + BITNO_32(present ^ next_present));
	
	switch (bit) {
	case IEEE80211_RADIOTAP_FLAGS:
	case IEEE80211_RADIOTAP_RATE:
	case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
	case IEEE80211_RADIOTAP_DB_ANTNOISE:
	case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
	case IEEE80211_RADIOTAP_DBM_ANTNOISE:
	case IEEE80211_RADIOTAP_ANTENNA:
	  u.u8 = *iter++;
	  break;
	case IEEE80211_RADIOTAP_DBM_TX_POWER:
	  u.i8 = *iter++;
	  break;
	case IEEE80211_RADIOTAP_CHANNEL:
	  u.u16 = EXTRACT_LE_16BITS(iter);
	  iter += sizeof(u.u16);
	  u2.u16 = EXTRACT_LE_16BITS(iter);
	  iter += sizeof(u2.u16);
	  break;
	case IEEE80211_RADIOTAP_FHSS:
	case IEEE80211_RADIOTAP_LOCK_QUALITY:
	case IEEE80211_RADIOTAP_TX_ATTENUATION:
	case IEEE80211_RADIOTAP_DB_TX_ATTENUATION:
	  u.u16 = EXTRACT_LE_16BITS(iter);
	  iter += sizeof(u.u16);
	  break;
	case IEEE80211_RADIOTAP_TSFT:
	  u.u64 = EXTRACT_LE_64BITS(iter);
	  iter += sizeof(u.u64);
		      break;
	default:
	  next_present = 0;
	  continue;
	}
	
	switch (bit) {
	case IEEE80211_RADIOTAP_CHANNEL:
	  printf("packet channel =%d\n", ieee80211_mhz2ieee(u.u16, u2.u16));
	  if (IEEE80211_IS_CHAN_FHSS(u2.u16))
	    printf(" carrier is 80211dsss\n");
	  else if (IEEE80211_IS_CHAN_A(u2.u16))
	    printf(" carrier is	80211a\n");
	  else if (IEEE80211_IS_CHAN_B(u2.u16))
	    printf(" carrier is carrier_80211b\n");
	  else if (IEEE80211_IS_CHAN_PUREG(u2.u16))
	    printf(" carrier is carrier_80211g\n");
	  else if (IEEE80211_IS_CHAN_G(u2.u16))
	    printf(" carrier is carrier_80211g\n");
	  else if (IEEE80211_IS_CHAN_T(u2.u16))
	    printf(" carrier is carrier_80211a\n");
	  else
	    printf(" carrier is carrier_unknown\n");
	  break;
	case IEEE80211_RADIOTAP_RATE:
	  printf("rate=%02x \n",u.u8 );
	  break;
	case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
	  printf("ant sign= %02x\n ",u.i8);
	  break;
	case IEEE80211_RADIOTAP_DB_ANTNOISE:
	  printf("noise =%02x \n ", u.i8);
	  break;
	case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
	  printf("signal = %02x\n",u.i8);
	  break;
	case IEEE80211_RADIOTAP_DBM_ANTNOISE:
	  printf("noise= %02x",u.i8);
	  break;
	case IEEE80211_RADIOTAP_FLAGS:
	  if (u.u8 & IEEE80211_RADIOTAP_F_FCS)
	    printf("fcs is 4 \n"); //fcs_cut = 4;
	  break;
	default:
	  break;
	}
      }
    }
    /*
    fcs_cut = 4 ;
*/
    /* copy data down over radiotap header */
    //    packet->caplen -= (EXTRACT_LE_16BITS(&hdr->it_len) + fcs_cut);
    //packet->len -= (EXTRACT_LE_16BITS(&hdr->it_len) + fcs_cut);
    //memcpy(packet->data, callback_data + EXTRACT_LE_16BITS(&hdr->it_len), packet->caplen);
    
    //		return 1;
    
    /*
      printf("\n");
      
      for (gh=0;gh<length; gh=gh+2){
      
      printf("%02x%02x ", gh, jb[gh],jb[gh+1]);
      }
      
      printf("\n---------------\n");
    */
    //	struct iphdr *iph = (struct iphdr*)(jb + 24) ;
    /*
      uchar * b;
      
      for( b = jb; b < jb+jb_len; ) {
      struct jigdump_hdr* jh = (struct jigdump_hdr*)b;
      if (jh->version_ != 55) {
      printf(" invalid jigdump_hdr (v= %d), got_snaplen=%d , discard\n",(int)jh->version_, jh->snaplen_);
      
      }else { 
      printf("got header %d, accept\n", (uint)jh->version_);
      }
      if (jh->hdrlen_ != 71) {
      printf(" jigdump hdr_len %d mis-match discard\n", (int)jh->hdrlen_);
      
      }
      //		printf("string is %s \n",jb);
      //		printf("ip version %d, protocol =%d \n", iph->version, iph->protocol) ;
      } */
  }

  return 0 ;
}






