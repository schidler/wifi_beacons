#include <unistd.h>
#include <error.h>
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
#include <syslog.h>
#include <linux/wireless.h>
#include <errno.h>
#include <math.h>
#include <pcap.h>
#include <ctype.h>
#include <inttypes.h>
#include <signal.h>
#include <zlib.h>
#include "util.h"
#include "ieee80211_radiotap.h"
#include "ieee80211.h"
#include "td-util.h"
#include "create-interface.h"
#include "pkts.h"
static int sequence_number = 0;
#define FILE_UPDATED "/tmp/sniffer/update.gz"
#define BISMARK_ID_FILENAME "/etc/bismark/ID"
#define UPDATE_FILENAME "/tmp/sniffer/updates/%s-%" PRIu64 "-%d.gz"
#define PENDING_UPDATE_FILENAME "/tmp/sniffer/current-update.gz"
static char bismark_id[256];
static int64_t start_timestamp_microseconds;
#define NUM_MICROS_PER_SECOND 1e6

int SLEEP_PERIOD =60 ; //default value
//#define MODE_DEBUG 0

static pthread_t signal_thread;
static pthread_t update_thread;
static pthread_mutex_t update_lock;

void mgmt_header_print(const u_char *p, const u_int8_t **srcp,  const u_int8_t **dstp, struct r_packet* paket)
{
    const struct mgmt_header_t *hp = (const struct mgmt_header_t *) p;
    
    if (srcp != NULL)
      *srcp = hp->sa;
    if (dstp != NULL)
      *dstp = hp->da;    

    u_char *ptr;
    ptr = hp->sa;
    //int i=0;
    char temp[18];
    //int y=0;
    //printf(" SA:*");
    //printf("**%02x:%02x:%02x:%02x:%02x:%02x**\n",ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5]);
    sprintf(temp,"%02x:%02x:%02x:%02x:%02x:%02x",ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5]);
      temp[17]='\0';
      //  printf("i is %d\n",i);
      //      printf("----temp is ---%s----\n",temp);
      memcpy(paket->mac_address,temp,strlen(temp));
#ifdef MODE_DEBUG
    printf("mac address =*%s* ",paket->mac_address );
    printf("BSSID: %s DA: %s SA: %s \n",
	   etheraddr_string((hp)->bssid), etheraddr_string((hp)->da),
	   etheraddr_string((hp)->sa));
#endif
}

void print_chaninfo(int freq, int flags, struct r_packet * paket)
{
  paket->freq= freq ;
#ifdef MODE_DEBUG
  printf("%u MHz", freq);
#endif

  if (IS_CHAN_FHSS(flags)){
    memcpy(paket->channel_info,"FHSS",4);
#ifdef MODE_DEBUG
    printf("FHSS");
#endif
  }
  if (IS_CHAN_A(flags)) {
    if (flags & IEEE80211_CHAN_HALF){
      memcpy(paket->channel_info,"A10M",4);
#ifdef MODE_DEBUG
      printf("A10M");
#endif
    }
    else if (flags & IEEE80211_CHAN_QUARTER){
      memcpy(paket->channel_info,"A5M",3);
#ifdef MODE_DEBUG
      printf("A5M");
#endif
    }
    else{
      memcpy(paket->channel_info,"A",1);
#ifdef MODE_DEBUG
      printf("A");
#endif
    }

  }
  if (IS_CHAN_ANYG(flags)){
    if (flags & IEEE80211_CHAN_HALF){
      memcpy(paket->channel_info,"G10M",4);
#ifdef MODE_DEBUG
      printf("G10M");
#endif
    }
    else if (flags & IEEE80211_CHAN_QUARTER){
      memcpy(paket->channel_info,"G5M",3);
#ifdef MODE_DEBUG
      printf("G5M");
#endif
    }
    else{
 memcpy(paket->channel_info,"G",1);
#ifdef MODE_DEBUG
      printf("G");
#endif
    }
  } else if (IS_CHAN_B(flags)){
    memcpy(paket->channel_info,"B",1);
#ifdef MODE_DEBUG
    printf("B");
#endif
  }
  if (flags & IEEE80211_CHAN_TURBO){
    memcpy(paket->channel_info,"T",1);
#ifdef MODE_DEBUG
    printf("T");
#endif
  }
  if (flags & IEEE80211_CHAN_HT20){
    memcpy(paket->channel_info,"HT20",4);
#ifdef MODE_DEBUG
    printf("HT20");
#endif
  }
  else if (flags & IEEE80211_CHAN_HT40D){
    memcpy(paket->channel_info,"HT4-",4);
#ifdef MODE_DEBUG
    printf("HT4-");
#endif
  }
  else if (flags & IEEE80211_CHAN_HT40U){
    memcpy(paket->channel_info,"HT4+",4);
#ifdef MODE_DEBUG
    printf("HT4+");
#endif
  }
  
}

void ieee_802_11_hdr_print(u_int16_t fc, const u_char *p, u_int hdrlen, const u_int8_t **srcp, const u_int8_t **dstp, struct r_packet *paket)
{
  int vflag;
  vflag=1;
  if (vflag) {
    if (FC_MORE_DATA(fc))
      if (FC_MORE_FLAG(fc)){
	paket->more_frag =1;
#ifdef MODE_DEBUG
	printf("More Fragments ");
#endif
	}
    if (FC_POWER_MGMT(fc)){
      paket->pwr_mgmt=1;
#ifdef MODE_DEBUG
      printf("PM");
#endif
}
    if (FC_RETRY(fc)){
      paket->retry=1;
#ifdef MODE_DEBUG
      printf("R ");
#endif
}
    if (FC_ORDER(fc)){
      paket->strictly_ordered=1;
#ifdef MODE_DEBUG
      printf("SO ");
#endif
}
    if (FC_WEP(fc)){
      paket->wep_enc=1;
#ifdef MODE_DEBUG
      printf("WEP Encrypted ");
#endif
}
    if (FC_TYPE(fc) != T_CTRL || FC_SUBTYPE(fc) != CTRL_PS_POLL){
#ifdef MODE_DEBUG
      printf(" dur: %d ", EXTRACT_LE_16BITS(&((const struct mgmt_header_t *)p)->duration));
#endif
} 
 }
  switch (FC_TYPE(fc)) {
  case T_MGMT:
    mgmt_header_print(p, srcp, dstp,paket);
    break;
  default:
#ifdef MODE_DEBUG
    printf("UH%d",FC_TYPE(fc));
#endif
    *srcp = NULL;
    *dstp = NULL;
    break;
  }
}

/* * Print out a null-terminated filename (or other ascii string). If ep is NULL, assume no truncation check is needed.
 * Return true if truncated.
 */
int
fn_print(register const u_char *s, register const u_char *ep, struct r_packet * paket)
{
  register int ret;
  register u_char c;
  char temp[48];
  int i = 0; 
  ret = 1;                        /* assume truncated */
 while (ep == NULL || s < ep) {
    c = *s++;
    if (c == '\0') {
      temp[i]=c ;
      ret = 0;
      break;
    }
    if (!isascii(c)) {
      c = toascii(c);
      temp[i]='-';//c prev
      continue; 
#ifdef MODE_DEBUG
      putchar('M');
      putchar('-');
#endif
    }
    if (!isprint(c)) {
      c ^= 0x40;      /* DEL to ?, others to alpha */
      temp[i]='^';//c prev
      continue; 
#ifdef MODE_DEBUG
      putchar('^');
#endif
    }
    temp[i]=c;
    #ifdef MODE_DEBUG
    putchar(c);
    #endif
    i++;
 }
 if(ret==1)
   temp[i]='\0';
 // printf("!!%s!!",temp);
  memcpy(paket->essid,temp, strlen(temp));
  return(ret);
}
//==============================================================
#define cpack_int8(__s, __p)    cpack_uint8((__s),  (u_int8_t*)(__p))

int cpack_init(struct cpack_state *, u_int8_t *, size_t);
int cpack_uint8(struct cpack_state *, u_int8_t *);
int cpack_uint16(struct cpack_state *, u_int16_t *);
int cpack_uint32(struct cpack_state *, u_int32_t *);
int cpack_uint64(struct cpack_state *, u_int64_t *);

u_int8_t * cpack_next_boundary(u_int8_t *buf, u_int8_t *p, size_t alignment)
{
  size_t misalignment = (size_t)(p - buf) % alignment;

  if (misalignment == 0)
    return p;

  return p + (alignment - misalignment);
}

u_int8_t * cpack_align_and_reserve(struct cpack_state *cs, size_t wordsize)
{
  u_int8_t *next;
  next = cpack_next_boundary(cs->c_buf, cs->c_next, wordsize);
  if (next - cs->c_buf + wordsize > cs->c_len)
    return NULL;

  return next;
}

int cpack_uint32(struct cpack_state *cs, u_int32_t *u)
{
  u_int8_t *next;

  if ((next = cpack_align_and_reserve(cs, sizeof(*u))) == NULL)
    return -1;

  *u = EXTRACT_LE_32BITS(next);
  cs->c_next = next + sizeof(*u);
  return 0;
}
int cpack_uint16(struct cpack_state *cs, u_int16_t *u)
{
  u_int8_t *next;

  if ((next = cpack_align_and_reserve(cs, sizeof(*u))) == NULL)
    return -1;

  *u = EXTRACT_LE_16BITS(next);

  cs->c_next = next + sizeof(*u);
  return 0;
}


int cpack_uint8(struct cpack_state *cs, u_int8_t *u)
{

  if ((size_t)(cs->c_next - cs->c_buf) >= cs->c_len)
    return -1;

  *u = *cs->c_next;
  cs->c_next++;
  return 0;
}


int
cpack_init(struct cpack_state *cs, u_int8_t *buf, size_t buflen)
{
  memset(cs, 0, sizeof(*cs));

  cs->c_buf = buf;
  cs->c_len = buflen;
  cs->c_next = cs->c_buf;

  return 0;
}

int cpack_uint64(struct cpack_state *cs, u_int64_t *u)
{
  u_int8_t *next;

  if ((next = cpack_align_and_reserve(cs, sizeof(*u))) == NULL)
    return -1;
  *u = EXTRACT_LE_64BITS(next);
  cs->c_next = next + sizeof(*u);
  return 0;
}
//========================================================

int parse_elements(struct mgmt_body_t *pbody, const u_char *p, int offset,u_int length)
{
  struct ssid_t ssid;
  struct challenge_t challenge;
  struct rates_t rates;
  struct ds_t ds;
  struct cf_t cf;
  struct tim_t tim;

  pbody->challenge_present = 0;
  pbody->ssid_present = 0;
  pbody->rates_present = 0;
  pbody->ds_present = 0;
  pbody->cf_present = 0;
  pbody->tim_present = 0;

  while (length != 0) {
    if (!TTEST2(*(p + offset), 1))
      return 0;
    if (length < 1)
      return 0;
    switch (*(p + offset)) {
    case E_SSID:
      if (!TTEST2(*(p + offset), 2))
	return 0;
      if (length < 2)
	return 0;
      memcpy(&ssid, p + offset, 2);
      offset += 2;
      length -= 2;
      if (ssid.length != 0) {
	if (ssid.length > sizeof(ssid.ssid) - 1)
	  return 0;
	if (!TTEST2(*(p + offset), ssid.length))
	  return 0;
	if (length < ssid.length)
	  return 0;
	memcpy(&ssid.ssid, p + offset, ssid.length);
	offset += ssid.length;
	length -= ssid.length;
      }
      ssid.ssid[ssid.length] = '\0';
      //
      if (!pbody->ssid_present) {
	pbody->ssid = ssid;
	pbody->ssid_present = 1;
      }
      break;
    case E_CHALLENGE:
      if (!TTEST2(*(p + offset), 2))
	return 0;
      if (length < 2)
	return 0;
      memcpy(&challenge, p + offset, 2);
      offset += 2;
      length -= 2;
      if (challenge.length != 0) {
	if (challenge.length >
	    sizeof(challenge.text) - 1)
	  return 0;
	if (!TTEST2(*(p + offset), challenge.length))
	  return 0;
	if (length < challenge.length)
	  return 0;
	memcpy(&challenge.text, p + offset,
	       challenge.length);
	offset += challenge.length;
	length -= challenge.length;
      }
      challenge.text[challenge.length] = '\0';
      //
      if (!pbody->challenge_present) {
	pbody->challenge = challenge;
	pbody->challenge_present = 1;
      }
      break;
    case E_RATES:
      if (!TTEST2(*(p + offset), 2))
	return 0;
      if (length < 2)
	return 0;
      memcpy(&rates, p + offset, 2);
      offset += 2;
      length -= 2;
      if (rates.length != 0) {
	if (rates.length > sizeof rates.rate)
	  return 0;
	if (!TTEST2(*(p + offset), rates.length))
	  return 0;
	if (length < rates.length)
	  return 0;
	memcpy(&rates.rate, p + offset, rates.length);
	offset += rates.length;
	length -= rates.length;
      }
      if (!pbody->rates_present && rates.length != 0) {
	pbody->rates = rates;
	pbody->rates_present = 1;
      }
      break;
    case E_DS:
      if (!TTEST2(*(p + offset), 3))
	return 0;
      if (length < 3)
	return 0;
      memcpy(&ds, p + offset, 3);
      offset += 3;
      length -= 3;
      if (!pbody->ds_present) {
	pbody->ds = ds;
	pbody->ds_present = 1;
      }
      break;
    case E_CF:
      if (!TTEST2(*(p + offset), 8))
	return 0;
      if (length < 8)
	return 0;
      memcpy(&cf, p + offset, 8);
      offset += 8;
      length -= 8;
      if (!pbody->cf_present) {
	pbody->cf = cf;
	pbody->cf_present = 1;
      }
      break;
    case E_TIM:
      if (!TTEST2(*(p + offset), 2))
	return 0;
      if (length < 2)
	return 0;
      memcpy(&tim, p + offset, 2);
      offset += 2;
      length -= 2;
      if (!TTEST2(*(p + offset), 3))
	return 0;
      if (length < 3)
	return 0;
      memcpy(&tim.count, p + offset, 3);
      offset += 3;
      length -= 3;

      if (tim.length <= 3)
	break;
      if (tim.length - 3 > (int)sizeof tim.bitmap)
	return 0;
      if (!TTEST2(*(p + offset), tim.length - 3))
	return 0;
      if (length < (u_int)(tim.length - 3))
	return 0;
      memcpy(tim.bitmap, p + (tim.length - 3),
	     (tim.length - 3));
      offset += tim.length - 3;
      length -= tim.length - 3;
      if (!pbody->tim_present) {
	pbody->tim = tim;
	pbody->tim_present = 1;
      }
      break;
    default:
      if (!TTEST2(*(p + offset), 2))
	return 0;
      if (length < 2)
	return 0;
      if (!TTEST2(*(p + offset + 2), *(p + offset + 1)))
	return 0;
      if (length < (u_int)(*(p + offset + 1) + 2))
	return 0;
      offset += *(p + offset + 1) + 2;
      length -= *(p + offset + 1) + 2;
      break;
    }
  }

  return 1;
}

void PRINT_HT_RATE(char* _sep,  u_int8_t _r, char* _suf,struct r_packet * paket){
#ifdef MODE_DEBUG
  printf("  %s%.1f%s ", _sep, (.5 * ieee80211_htrates[(_r) & 0xf]), _suf);
#endif  
  paket->rate=(.5 * ieee80211_htrates[(_r) & 0xf]);
  
}

void PRINT_SSID( struct mgmt_body_t p,struct r_packet* paket){ 
  if (p.ssid_present) { 
#ifdef MODE_DEBUG   
    printf(" ( "); 
#endif
    
    fn_print(p.ssid.ssid, NULL,paket); 
#ifdef MODE_DEBUG
    printf(")"); 
#endif
  }
}

void PRINT_RATE(char* _sep,  u_int8_t _r, char* _suf,struct r_packet * paket ) {
#ifdef MODE_DEBUG
  printf("  %s%2.1f%s ", _sep, (.5 * ((_r) & 0x7f)), _suf);
#endif
  //  printf("**%2.1f**",(.5 * ((_r) & 0x7f)));
  paket->rate=(float)((.5 * ((_r) & 0x7f)));
  
}

//call to this function is commented out 
void PRINT_RATES(struct mgmt_body_t p, struct r_packet* paket) {
  if (p.rates_present) {
  int z; 
 const char *sep = " ["; 
  for (z = 0; z < p.rates.length ; z++) { 
    PRINT_RATE(sep, p.rates.rate[z], (p.rates.rate[z] & 0x80 ? "*" : ""),paket); 
  sep = " "; 
  } 
  if (p.rates.length != 0) 
    printf(" Mbit] "); 
  }

}
void PRINT_DS_CHANNEL( struct mgmt_body_t  p, struct r_packet* paket){
  if (p.ds_present) {
#ifdef MODE_DEBUG
    printf(" CH: %u", p.ds.channel);
#endif
    paket->channel= p.ds.channel;
  } 
#ifdef MODE_DEBUG
  printf("%s ", CAPABILITY_PRIVACY(p.capability_info) ? ", PRIVACY" : "" );
#endif
  paket->cap_privacy=  CAPABILITY_PRIVACY(p.capability_info) ? 1 :0 ;
}

int handle_beacon(const u_char *p, u_int length, struct r_packet * paket)
{
  struct mgmt_body_t pbody;
  int offset = 0;
  int ret;
  
  memset(&pbody, 0, sizeof(pbody));
	
  if (!TTEST2(*p, IEEE802_11_TSTAMP_LEN + IEEE802_11_BCNINT_LEN +
	      IEEE802_11_CAPINFO_LEN))
    return 0;
  if (length < IEEE802_11_TSTAMP_LEN + IEEE802_11_BCNINT_LEN +
      IEEE802_11_CAPINFO_LEN)
    return 0;
  memcpy(&pbody.timestamp, p, IEEE802_11_TSTAMP_LEN);
  offset += IEEE802_11_TSTAMP_LEN;
  length -= IEEE802_11_TSTAMP_LEN;
  pbody.beacon_interval = EXTRACT_LE_16BITS(p+offset);
  offset += IEEE802_11_BCNINT_LEN;
  length -= IEEE802_11_BCNINT_LEN;
  pbody.capability_info = EXTRACT_LE_16BITS(p+offset);
  offset += IEEE802_11_CAPINFO_LEN;
  length -= IEEE802_11_CAPINFO_LEN;
  
  ret = parse_elements(&pbody, p, offset, length);
  
  PRINT_SSID(pbody,paket);
  PRINT_DS_CHANNEL(pbody,paket);
  
#ifdef MODE_DEBUG
  PRINT_RATES(pbody,paket);
  printf(" %s",	 CAPABILITY_ESS(pbody.capability_info) ? "ESS" : "IBSS");
#endif
  paket->cap_ess_ibss =55;
  paket->cap_ess_ibss=  CAPABILITY_ESS(pbody.capability_info) ? 1:2;
  return ret;
}

int mgmt_body_print(u_int16_t fc, const struct mgmt_header_t *pmh, const u_char *p, u_int length, struct r_packet * paket)
{
  switch (FC_SUBTYPE(fc)) {
  case ST_BEACON:
//    printf("Beacon");
    return handle_beacon(p, length, paket);
  }
  return 0; 
}

u_int ieee802_11_print(const u_char *p, u_int length, u_int orig_caplen, int pad, u_int fcslen, struct r_packet * paket)
{
  u_int16_t fc;
  u_int caplen, hdrlen;
  const u_int8_t *src, *dst;
  
  caplen = orig_caplen;
  /* Remove FCS, if present */
  if (length < fcslen) {
#ifdef MODE_DEBUG
    printf("len<fcslen");
#endif
    return caplen;
        }
  length -= fcslen;
  if (caplen > length) {
    /* Amount of FCS in actual packet data, if any */
    fcslen = caplen - length;
    caplen -= fcslen;
    snapend -= fcslen;
  }
  
  if (caplen < IEEE802_11_FC_LEN) {
#ifdef MODE_DEBUG
    printf("cap<fcslen");
#endif
    return orig_caplen;
  }

  fc = EXTRACT_LE_16BITS(p);
  hdrlen = MGMT_HDRLEN; //extract_header_length(fc);
  if (pad)
    hdrlen = roundup2(hdrlen, 4);
      
  if (caplen < hdrlen) {
#ifdef MODE_DEBUG
    printf("caplen<hdrlen");
#endif
    return hdrlen;
  }
  ieee_802_11_hdr_print(fc, p, hdrlen, &src, &dst,paket);
  length -= hdrlen;
  caplen -= hdrlen;
  p += hdrlen;
  
  switch (FC_TYPE(fc)) {
  case T_MGMT:
    if (!mgmt_body_print(fc,
			 (const struct mgmt_header_t *)(p - hdrlen), p, length,paket)) {
#ifdef MODE_DEBUG
      printf("[|802.11]");
#endif
      return hdrlen;
    }
    break;
  default:
#ifdef MODE_DEBUG
    printf("UH (%d)", FC_TYPE(fc)); //unknown header
#endif
    break;
  }
  
  return hdrlen;
}

int print_radiotap_field(struct cpack_state *s, u_int32_t bit, u_int8_t *flags, struct r_packet* paket)
{
  union {
    int8_t          i8;
    u_int8_t        u8;
    int16_t         i16;
    u_int16_t       u16;
    u_int32_t       u32;
    u_int64_t       u64;
  } u, u2, u3, u4;
  int rc;
  switch (bit) {
  case IEEE80211_RADIOTAP_FLAGS:
    rc = cpack_uint8(s, &u.u8);
    *flags = u.u8;
    break;
  case IEEE80211_RADIOTAP_RATE:
  case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
  case IEEE80211_RADIOTAP_DB_ANTNOISE:
  case IEEE80211_RADIOTAP_ANTENNA:
    rc = cpack_uint8(s, &u.u8);
    break;
  case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
  case IEEE80211_RADIOTAP_DBM_ANTNOISE:
    rc = cpack_int8(s, &u.i8);
    break;
  case IEEE80211_RADIOTAP_CHANNEL:
    rc = cpack_uint16(s, &u.u16);
    if (rc != 0)
      break;
    rc = cpack_uint16(s, &u2.u16);
    break;
  case IEEE80211_RADIOTAP_FHSS:
  case IEEE80211_RADIOTAP_LOCK_QUALITY:
  case IEEE80211_RADIOTAP_TX_ATTENUATION:
    rc = cpack_uint16(s, &u.u16);
    break;
  case IEEE80211_RADIOTAP_DB_TX_ATTENUATION:
    rc = cpack_uint8(s, &u.u8);
    break;
  case IEEE80211_RADIOTAP_DBM_TX_POWER:
    rc = cpack_int8(s, &u.i8);
    break;
  case IEEE80211_RADIOTAP_TSFT:
    rc = cpack_uint64(s, &u.u64);
    break;
  case IEEE80211_RADIOTAP_XCHANNEL:
    rc = cpack_uint32(s, &u.u32);
    if (rc != 0)
      break;
    rc = cpack_uint16(s, &u2.u16);
    if (rc != 0)
      break;
    rc = cpack_uint8(s, &u3.u8);
    if (rc != 0)
      break;
    rc = cpack_uint8(s, &u4.u8);
    break;
  default:
    // this bit indicates a field whos size we do not know, so we cannot proceed.  Just print the bit number.     
#ifdef MODE_DEBUG
    printf("[bit %u] ", bit);
#endif
    return -1;
  }
  if (rc != 0) {
#ifdef MODE_DEBUG
    printf("[|802.11]");
#endif
    return rc;
  }

  switch (bit) {
  case IEEE80211_RADIOTAP_CHANNEL:
    print_chaninfo(u.u16, u2.u16,paket);
    break;
  case IEEE80211_RADIOTAP_FHSS:
#ifdef MODE_DEBUG
    printf("fhset %d fhpat %d ", u.u16 & 0xff, (u.u16 >> 8) & 0xff);
#endif
    break;
  case IEEE80211_RADIOTAP_RATE:
    if (u.u8 & 0x80){    
      //paket->rate=u.u8;
      //#ifdef MODE_DEBUG
      PRINT_HT_RATE("", u.u8, "Mb/s ",paket);
      //#endif
    }
    else{    
      PRINT_RATE("", u.u8, "Mb/s ",paket);
      //  printf(" rate is %f main ", paket->rate);
      //printf("rate **%2.1f**\n", paket->rate);
    }    
    break;
  case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
    paket->dbm_sig=u.i8;
#ifdef MODE_DEBUG
    printf("%ddB  signal ", u.i8);
#endif
    break;
  case IEEE80211_RADIOTAP_DBM_ANTNOISE:
    paket->dbm_noise=u.i8;
#ifdef MODE_DEBUG
    printf("%ddB  noise ", u.i8);
#endif
    break;
  case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
    paket->db_sig=u.u8;
#ifdef MODE_DEBUG
    printf("%ddB signal ", u.u8);
#endif
    break;
  case IEEE80211_RADIOTAP_DB_ANTNOISE:
    paket->db_noise=u.u8;
#ifdef MODE_DEBUG
    printf("%ddB noise ", u.u8);
#endif
    break;
  case IEEE80211_RADIOTAP_LOCK_QUALITY:
#ifdef MODE_DEBUG
    printf("%u sq ", u.u16);
#endif
    break;
  case IEEE80211_RADIOTAP_TX_ATTENUATION:
#ifdef MODE_DEBUG
    printf("%d tx power ", -(int)u.u16);
#endif
    break;
  case IEEE80211_RADIOTAP_DB_TX_ATTENUATION:
#ifdef MODE_DEBUG
    printf("%ddB tx power ", -(int)u.u8);
#endif
    break;
  case IEEE80211_RADIOTAP_DBM_TX_POWER:
#ifdef MODE_DEBUG
    printf("%ddBm tx power ", u.i8);
#endif
    break;
  case IEEE80211_RADIOTAP_FLAGS:
    if (u.u8 & IEEE80211_RADIOTAP_F_CFP){
      paket->cfp_err=1;
#ifdef MODE_DEBUG
      printf("cfp ");
#endif
    }
    if (u.u8 & IEEE80211_RADIOTAP_F_SHORTPRE){
      paket->short_preamble_err =1;
#ifdef MODE_DEBUG
      printf("short preamble ");
#endif
    }
    if (u.u8 & IEEE80211_RADIOTAP_F_WEP){
      paket->radiotap_wep_err =1;
#ifdef MODE_DEBUG
      printf("wep ");
#endif
  }
    if (u.u8 & IEEE80211_RADIOTAP_F_FRAG){
      paket->frag_err=1;
#ifdef MODE_DEBUG
      printf("fragmented ");
#endif
    }
    if (u.u8 & IEEE80211_RADIOTAP_F_BADFCS){
      paket->bad_fcs_err=1;
#ifdef MODE_DEBUG
      printf("bad-fcs ");
#endif
    }
    break;
  case IEEE80211_RADIOTAP_ANTENNA:
    paket->antenna= u.u8;
#ifdef MODE_DEBUG
    printf("antenna %d ", u.u8);
#endif
    break;
  case IEEE80211_RADIOTAP_TSFT:
#ifdef MODE_DEBUG
    printf(/*% PRIu64 */" tsft "/*, u.u64*/);
#endif
    break;
  case IEEE80211_RADIOTAP_XCHANNEL:
    print_chaninfo(u2.u16, u.u32,paket);
    break;
  }
  return 0;
}

u_int ieee802_11_radio_print(const u_char *p, u_int length, u_int caplen, struct r_packet* paket)
{
#define BITNO_32(x) (((x) >> 16) ? 16 + BITNO_16((x) >> 16) : BITNO_16((x)))
#define BITNO_16(x) (((x) >> 8) ? 8 + BITNO_8((x) >> 8) : BITNO_8((x)))
#define BITNO_8(x) (((x) >> 4) ? 4 + BITNO_4((x) >> 4) : BITNO_4((x)))
#define BITNO_4(x) (((x) >> 2) ? 2 + BITNO_2((x) >> 2) : BITNO_2((x)))
#define BITNO_2(x) (((x) & 2) ? 1 : 0)
#define BIT(n)  (1U << n)
#define IS_EXTENDED(__p)        \
  (EXTRACT_LE_32BITS(__p) & BIT(IEEE80211_RADIOTAP_EXT)) != 0

  struct cpack_state cpacker;
  struct ieee80211_radiotap_header *hdr;
  u_int32_t present, next_present;
  u_int32_t *presentp, *last_presentp;
  enum ieee80211_radiotap_type bit;
  int bit0;
  const u_char *iter;
  u_int len;
  u_int8_t flags;
  int pad;
  u_int fcslen;

  if (caplen < sizeof(*hdr)) {
#ifdef MODE_DEBUG
    printf("caplen<hdr");
#endif
    return caplen;
  }
  hdr = (struct ieee80211_radiotap_header *)p;
  len = EXTRACT_LE_16BITS(&hdr->it_len);
  if (caplen < len) {
#ifdef MODE_DEBUG
    printf("caplen<len");
#endif
    return caplen;
  }
  for (last_presentp = &hdr->it_present;
       IS_EXTENDED(last_presentp) &&
	 (u_char*)(last_presentp + 1) <= p + len;
       last_presentp++);
  if (IS_EXTENDED(last_presentp)) {
#ifdef MODE_DEBUG
    printf("more bitmap ext than bytes");
#endif
    return caplen;
  }
  iter = (u_char*)(last_presentp + 1);

  if (cpack_init(&cpacker, (u_int8_t*)iter, len - (iter - p)) != 0) {
    /* XXX */
#ifdef MODE_DEBUG
    printf("XXX");
#endif
    return caplen;
  }

  flags = 0;
  pad = 0;
  fcslen = 0;
  for (bit0 = 0, presentp = &hdr->it_present; presentp <= last_presentp;
       presentp++, bit0 += 32) {
    for (present = EXTRACT_LE_32BITS(presentp); present;
	 present = next_present) {
      next_present = present & (present - 1);
      bit = (enum ieee80211_radiotap_type)
	(bit0 + BITNO_32(present ^ next_present));

      if (print_radiotap_field(&cpacker, bit, &flags,paket) != 0)
	goto out;
    }
  }

  if (flags & IEEE80211_RADIOTAP_F_DATAPAD)
    pad = 1;
  if (flags & IEEE80211_RADIOTAP_F_FCS)
    fcslen = 4;
 out:
  return len + ieee802_11_print(p + len, length - len, caplen - len, pad,fcslen,paket);
#undef BITNO_32
#undef BITNO_16
#undef BITNO_8
#undef BITNO_4
#undef BITNO_2
#undef BIT
}


void address_table_init(address_table_t* table) {
  memset(table, '\0', sizeof(*table));
}
#define MODULUS(m, d)  ((((m) % (d)) + (d)) % (d))
#define NORM(m)  (MODULUS(m, MAC_TABLE_ENTRIES))

int address_table_lookup(address_table_t*  table,struct r_packet* paket) {
  char m_address[sizeof(paket->mac_address)];

   printf("In lookup %s\n", paket->mac_address);
   printf("Must be assci **** %s ****\n", paket->essid);

  memcpy(m_address,paket->mac_address,sizeof(paket->mac_address));

  if (table->length > 0) {
    /* Search table starting w/ most recent MAC addresses. */
    int idx;
    for (idx = 0; idx < table->length; ++idx) {
      int mac_id = NORM(table->last - idx);
      if (!memcmp(table->entries[mac_id].mac_add, m_address, sizeof(m_address))) {
	//memcpy(table->entries[mac_id].mac_add, m_address, sizeof(m_address));
	table->entries[mac_id].packet_count++;
	if(paket->bad_fcs_err)
	  table->entries[mac_id].bad_fcs_err_count++;
	if(paket->short_preamble_err)
	  table->entries[mac_id].short_preamble_err_count++;
	if(paket->radiotap_wep_err)
	  table->entries[mac_id].radiotap_wep_err_count++;
	if(paket->frag_err)
	  table->entries[mac_id].frag_err_count++;
	if( paket->cfp_err)
	  table->entries[mac_id].cfp_err_count++ ;
	if(paket->retry)
	  table->entries[mac_id].retry_err_count++; 
	if(paket->strictly_ordered)
	  table->entries[mac_id].strictly_ordered_err=paket->strictly_ordered;
	if(paket->pwr_mgmt)
	  table->entries[mac_id].pwr_mgmt_count++;
	if(paket->wep_enc)
	  table->entries[mac_id].wep_enc_count++;
	if(paket->more_frag)
	  table->entries[mac_id].more_frag_count++;
	table->entries[mac_id].db_signal_sum = table->entries[mac_id].db_signal_sum+ paket->db_sig; 

	table->entries[mac_id].db_noise_sum= table->entries[mac_id].db_noise_sum +paket->db_noise;
	
	table->entries[mac_id].dbm_noise_sum =	table->entries[mac_id].dbm_noise_sum + paket->dbm_noise ;

	table->entries[mac_id].dbm_signal_sum =(float)-(paket->dbm_sig) + table->entries[mac_id].dbm_signal_sum ;
	table->entries[mac_id].rate = table->entries[mac_id].rate +paket->rate ;
	
	//printf("sig after %2.1f \n",table->entries[mac_id].dbm_signal_sum) ;
#if 0
	printf("Before essid  %s,  %s \n",table->entries[mac_id].essid,paket->essid);
	printf("mac address  %s \n",table->entries[mac_id].mac_add);
	printf("pkt count=%d,\n", table->entries[mac_id].packet_count);
	
	memcpy(table->entries[mac_id].essid, paket->essid, sizeof(paket->essid));
	memcpy(table->entries[mac_id].mac_add, m_address, sizeof(m_address));

	printf("After essid of existing  %s,  %s \n",table->entries[mac_id].essid,paket->essid );
#endif
        return mac_id;
      }
    }
  }
  if (table->length == MAC_TABLE_ENTRIES) {
    /* Discard the oldest MAC address. */
    table->first = NORM(table->first + 1);
  } else {
    ++table->length;
  }
  if (table->length > 1) {
    table->last = NORM(table->last + 1);
  }

  memcpy(table->entries[table->last].essid, paket->essid, sizeof(paket->essid)); 
  memcpy(table->entries[table->last].mac_add, paket->mac_address, sizeof(m_address));
  table->entries[table->last].packet_count =  table->entries[table->last].packet_count+1;
  
  table->entries[table->last].db_signal_sum=paket->db_sig; 
  table->entries[table->last].db_noise_sum=paket->db_noise;
 
  table->entries[table->last].dbm_noise_sum =paket->dbm_noise ;
  table->entries[table->last].dbm_signal_sum =((float)-(paket->dbm_sig));    
  //  printf("Essid first time : %s , %s\n",table->entries[table->last].essid,paket->essid );  
  //counters 
  table->entries[table->last].bad_fcs_err_count=paket->bad_fcs_err;    
  table->entries[table->last].short_preamble_err_count = paket->short_preamble_err;
  table->entries[table->last].radiotap_wep_err_count= paket->radiotap_wep_err;
  table->entries[table->last].frag_err_count =paket->frag_err;
  table->entries[table->last].cfp_err_count = paket->cfp_err ;
  table->entries[table->last].retry_err_count =paket->retry ;  
  table->entries[table->last].strictly_ordered_err=paket->strictly_ordered;
  table->entries[table->last].pwr_mgmt_count =paket->pwr_mgmt;
  table->entries[table->last].wep_enc_count=paket->wep_enc;
  table->entries[table->last].more_frag_count= paket->more_frag;

  table->entries[table->last].cap_privacy =paket->cap_privacy;
  table->entries[table->last].cap_ess_ibss =paket->cap_ess_ibss;
  table->entries[table->last].freq =paket->freq ; 
  table->entries[table->last].channel= paket->channel;
  memcpy(table->entries[table->last].channel_info, paket->channel_info, sizeof(paket->channel_info));
  //printf("packet rate is %f \n",paket->rate );
  table->entries[table->last].rate = paket->rate ;

  if (table->added_since_last_update < MAC_TABLE_ENTRIES) {
    ++table->added_since_last_update;
  }
  return table->last;
}

static int initialize_bismark_id() {  
  FILE* handle = fopen(BISMARK_ID_FILENAME, "r");
  if (!handle) {
    perror("Cannot open Bismark ID file " BISMARK_ID_FILENAME);
    return -1;
  }
  if(fscanf(handle, "%255s\n", bismark_id) < 1) {
    perror("Cannot read Bismark ID file " BISMARK_ID_FILENAME);
    return -1;
  }
  fclose(handle);
  
  return 0;
}

int address_table_write_update(address_table_t* table,gzFile handle) {

  static char buff[1024]; /*minimize stack size*/
  static long prev_crc_err = 0;
  static long prev_phy_err = 0;
  static long prev_rx_pkts_all = 0;
  static long prev_rx_bytes_all = 0;
  FILE *fproc = NULL;

  long phy_err_delta=0;
  long crc_err_delta=0;
  long rx_pkts_all_delta=0;
  long rx_bytes_all_delta=0;

  typedef long u_int;
  u_int phy_err=0;
  u_int crc_err=0;
  u_int rx_pkts_all=0;
  u_int rx_bytes_all=0;

  if((fproc = fopen("/sys/kernel/debug/ieee80211/phy0/ath9k/recv", "r")) == NULL )
    return -1;

  while ((fgets(buff, sizeof(buff), fproc)) != NULL) {
    if (strncmp(buff,"           CRC ERR :",18) == 0) {
      sscanf(buff,"           CRC ERR :%u ", &crc_err);     
    }


    if (strncmp(buff,"           PHY ERR :",18) == 0) {
      sscanf(buff,"           PHY ERR :%u ", &phy_err);  
    }

    if (strncmp(buff,"       RX-Pkts-All :", 18) == 0) {
      sscanf(buff,"       RX-Pkts-All :%u ", &rx_pkts_all);
    }
    if (strncmp(buff,"      RX-Bytes-All :", 18) == 0) {
      sscanf(buff,"      RX-Bytes-All :%u ", &rx_bytes_all);
    }
  }
  fclose(fproc);

  phy_err_delta= phy_err - prev_phy_err ;

  crc_err_delta= crc_err - prev_crc_err;
  rx_pkts_all_delta=  rx_pkts_all - prev_rx_pkts_all ;
  rx_bytes_all_delta=   rx_bytes_all - prev_rx_bytes_all ;


  prev_crc_err =crc_err ;
  prev_phy_err =    phy_err ;
  prev_rx_pkts_all =  rx_pkts_all ;
  prev_rx_bytes_all =rx_bytes_all ;

#if 0
  printf("crc_err is %ld\n",crc_err);
  printf("phy_err is %ld\n",phy_err);
  printf("rx_bytes_all is %ld\n",rx_bytes_all);
  printf("rx_pkts_all is %ld\n",rx_pkts_all);

  printf("prev, crc_err_delta is %ld %ld\n",prev_crc_err, crc_err_delta);
  printf("prev, phy_err_delta is %ld %ld\n",prev_phy_err, phy_err_delta);
  printf("prev, rx_bytes_all_delta is %ld %ld\n",prev_rx_bytes_all,rx_bytes_all_delta);
  printf("prev, rx_pkts_all_delta is %ld %ld\n",prev_rx_pkts_all,rx_pkts_all_delta);
#endif 
  if(!gzprintf(handle,"%d|%d|%d|%d\n",crc_err_delta, phy_err_delta,rx_bytes_all_delta,rx_pkts_all_delta))
    {
      perror("error writing the zip file :from /sys");
      exit(0);
    }
     
  int idx;
  for (idx = table->added_since_last_update; idx > 0; --idx) {
    int mac_id = NORM(table->last - idx + 1);
    
    //#if 0
    printf("%s|%s|privacy%u|ibss%u|f%u|c%u|%s|r%2.1f",table->entries[mac_id].mac_add,
	   table->entries[mac_id].essid, 
	   table->entries[mac_id].cap_privacy,
	   table->entries[mac_id].cap_ess_ibss, 
	   table->entries[mac_id].freq ,
	   table->entries[mac_id].channel,
	   table->entries[mac_id].channel_info, 
	   table->entries[mac_id].rate);
    
   printf("pc%d|bfs%d|sp%d|wep%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|noise%d|%2.1f|%2.1f\n",
	  table->entries[mac_id].packet_count,
	  table->entries[mac_id].bad_fcs_err_count,
	  table->entries[mac_id].short_preamble_err_count,
	  table->entries[mac_id].radiotap_wep_err_count,
	  table->entries[mac_id].frag_err_count,
	  table->entries[mac_id].cfp_err_count,
	  table->entries[mac_id].retry_err_count,
	  table->entries[mac_id].strictly_ordered_err,
	  table->entries[mac_id].pwr_mgmt_count, 
	  table->entries[mac_id].wep_enc_count,
	  table->entries[mac_id].more_frag_count,
	  table->entries[mac_id].db_signal_sum,
	  table->entries[mac_id].db_noise_sum,	
	  table->entries[mac_id].dbm_noise_sum ,
	  table->entries[mac_id].dbm_signal_sum, (table->entries[mac_id].dbm_signal_sum/table->entries[mac_id].packet_count));

#if 0
   printf("**%s %f %d %f**\n", table->entries[mac_id].mac_add, table->entries[mac_id].dbm_signal_sum,table->entries[mac_id].packet_count,
 	  table->entries[mac_id].dbm_signal_sum/ table->entries[mac_id].packet_count);
#endif
   
   if(!gzprintf(handle,"%s|%s|%u|%u|%d|%d|%s|%2.1f",table->entries[mac_id].mac_add,
	   table->entries[mac_id].essid, 
	   table->entries[mac_id].cap_privacy,
	   table->entries[mac_id].cap_ess_ibss, 
	   table->entries[mac_id].freq ,
	   table->entries[mac_id].channel,
	   table->entries[mac_id].channel_info, 
		(table->entries[mac_id].rate/table->entries[mac_id].packet_count))){
     perror("error writing the zip file ");
     exit(0);

   }    
   if(!gzprintf(handle,"|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%2.1f|%2.1f \n",
	  table->entries[mac_id].packet_count,
	  table->entries[mac_id].bad_fcs_err_count,
	  table->entries[mac_id].short_preamble_err_count,
	  table->entries[mac_id].radiotap_wep_err_count,
	  table->entries[mac_id].frag_err_count,
	  table->entries[mac_id].cfp_err_count,
	  table->entries[mac_id].retry_err_count,
	  table->entries[mac_id].strictly_ordered_err,
	  table->entries[mac_id].pwr_mgmt_count, 
	  table->entries[mac_id].wep_enc_count,
	  table->entries[mac_id].more_frag_count,
	  table->entries[mac_id].db_signal_sum,
	  table->entries[mac_id].db_noise_sum,	
		table->entries[mac_id].dbm_noise_sum , (table->entries[mac_id].dbm_signal_sum/table->entries[mac_id].packet_count))){
     perror("error writing the zip file");
     exit(0);
   }
  }  
  return 0; 
}

void write_update(){
printf("*********************wrote update **************************\n");

gzFile handle = gzopen (PENDING_UPDATE_FILENAME, "wb");
 if (!handle) {
   //#ifndef MODE_DEBUG
   perror("Could not open update file for writing\n");
   //#endif
   exit(1);
 }
 time_t current_timestamp = time(NULL);
 if (!gzprintf(handle,"%s %" PRId64 " %d %" PRId64 "\n",bismark_id,start_timestamp_microseconds,sequence_number,(int64_t)current_timestamp)) {
   perror("Error writing update");
   exit(1);
 }

 address_table_write_update(&address_table,handle);
 gzclose(handle);

 char update_filename[FILENAME_MAX];
 snprintf(update_filename,FILENAME_MAX,UPDATE_FILENAME,bismark_id,start_timestamp_microseconds,sequence_number);
 if (rename(PENDING_UPDATE_FILENAME, update_filename)) {
   perror("Could not stage update");
   exit(1);
 }
 ++sequence_number;
 address_table_init(&address_table);

}

static void* updater(void* arg) {
  while (1) {
    sleep(SLEEP_PERIOD);
    if (pthread_mutex_lock(&update_lock)) {
      perror("Error acquiring mutex for update");
      exit(1);
    }
    write_update();

    if (pthread_mutex_unlock(&update_lock)) {
      perror("Error unlocking update mutex");
      exit(1);
    }
  }
}

static void* handle_signals(void* arg) {
  sigset_t* signal_set = (sigset_t*)arg;
  int signal_handled;
  while (1) {
    if (sigwait(signal_set, &signal_handled)) {
      perror("Error handling signal");
      continue;
    }
    if (pthread_mutex_lock(&update_lock)) {
      perror("Error acquiring mutex for update");
      exit(1);
    }
    write_update();
    exit(0);
  }
}


void process_packet (u_char * args, const struct pcap_pkthdr *header, const u_char * packet)
{
  snapend = packet+ header->caplen; 
  struct r_packet paket ; 
  memset(&paket,'\0',sizeof(paket));
  ieee802_11_radio_print(packet, header->len, header->caplen,&paket);

#if 0
  printf("\npacket has signal %d\n",paket.dbm_sig) ;
  printf("MAC: **%s** \n",paket.mac_address);
  printf("essid: %s \n" ,paket.essid) ;
  printf("freq %u \n", paket.freq) ;
  printf("db sig %u \n", paket.db_sig);
  printf("db_noise %u \n",paket.db_noise);
  printf("dbm sig %d \n", paket.dbm_sig);
  printf("dbm noise %d \n", paket.dbm_noise);
  printf("rate %u \n ", paket.rate);
  //printf("antenna %u \n ", paket.antenna);
  printf("fcs %u \n", paket.bad_fcs_err);
 
  printf("sh pr %u \n",paket.short_preamble_err);
  printf("wep err %u \n",paket.radiotap_wep_err);
  printf("frag %u \n",paket.frag_err);
  printf("cfp %u \n",paket.cfp_err) ;
  printf("bssid_cap %d\n",paket.cap_ess_ibss);  
  printf("privacy flag %d \n",paket.cap_privacy );
  printf("channel: %u \n",paket.channel);
  printf("str ordered %d \n",paket.strictly_ordered);
  printf("pw mgmt %d \n",paket.pwr_mgmt);
  printf("chan info *%s* \n",paket.channel_info);
#endif

  address_table_lookup(&address_table,&paket);

#ifdef MODE_DEBUG
  printf("\n------------------------------------\n");
#endif

}


int instantiating_pcap (char* device){
  checkup(device);
  initialize_bismark_id();
  char errbuf[PCAP_ERRBUF_SIZE]; 
  bpf_u_int32 netp;   
  bpf_u_int32 maskp;  
  struct bpf_program fp; 
  int r;  
  pcap_t *handle;
  char *filter = "type mgt subtype beacon"; //the awesome one liner
  address_table_init(&address_table);
  if (device == NULL) {
    device = pcap_lookupdev (errbuf); 
    if (device == NULL){  fprintf (stderr,"%s", errbuf); exit (1);}
  }
  handle = pcap_open_live (device, BUFSIZ,1,0,errbuf); 
  if (handle == NULL) { fprintf (stderr, "%s", errbuf);
    exit (1);
  }
  if (pcap_compile (handle, &fp, filter, 0, maskp) == -1){
      fprintf (stderr, "Compile: %s\n", pcap_geterr (handle)); exit (1);
  }
  
  if (pcap_setfilter (handle, &fp) == -1){
    fprintf (stderr, "Setfilter: %s", pcap_geterr (handle)); exit (1);
  }
  pcap_freecode (&fp);
  
  if ((r = pcap_loop(handle, -1, process_packet, NULL)) < 0){
    if (r == -1){  fprintf (stderr, "Loop: %s", pcap_geterr (handle)); 
exit (1);
    } // -2 : breakoff from pcap loop
  }
  pcap_close (handle);
  return 0 ;
}



int main(int argc, char* argv[])
{
  if (argc<3){
    printf("Usage: sniffer <interface> <time interval(seconds)> \n");
    exit(1); 
  }
  char *device= argv[1];  
  SLEEP_PERIOD= atoi(argv[2]);

  sigset_t signal_set;
  struct timeval start_timeval;
  gettimeofday(&start_timeval, NULL);
  start_timestamp_microseconds
    = start_timeval.tv_sec * NUM_MICROS_PER_SECOND + start_timeval.tv_usec;

  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  sigaddset(&signal_set, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &signal_set, NULL)) {
    perror("Error calling pthread_sigmask");
    return 1;
  }
  if (pthread_create(&signal_thread, NULL, handle_signals, &signal_set)) {
    perror("Error creating signal handling thread");
    return 1;
  }
  if (pthread_create(&update_thread, NULL, updater, NULL)) {
    perror("Error creating updates thread");
    return 1;
  }
  instantiating_pcap (device);
  
  return 0 ;
}
