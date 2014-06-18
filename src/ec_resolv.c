/*
    ettercap -- name resolution module

    Copyright (C) ALoR & NaGA

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#include <ec.h>
#include <ec_resolv.h>
#include <ec_hash.h>

#ifndef OS_WINDOWS
   #include <netdb.h>
#endif

#define TABBIT    9 /* 2^9 bit tab entries: 512 SLISTS */
#define TABSIZE   (1UL<<TABBIT)
#define TABMASK   (TABSIZE-1) /* to mask fnv_1 hash algorithm */

/* globals */

static SLIST_HEAD(, resolv_entry) resolv_cache_head[TABSIZE];

struct resolv_entry {
   struct ip_addr ip;
   char *hostname;
   SLIST_ENTRY(resolv_entry) next;
};

/* protos */

static int resolv_cache_search(struct ip_addr *ip, char *name);

/************************************************/

/*
 * resolves an ip address into an hostname.
 * before doing the real getnameinfo it search in
 * a cache of previously resolved hosts to increase
 * speed.
 * after each getnameinfo the result is inserted 
 * in the cache.
 */

int host_iptoa(struct ip_addr *ip, char *name)
{
   struct sockaddr_storage ss;
   struct sockaddr_in *sa4;
   struct sockaddr_in6 *sa6;
   char tmp[MAX_ASCII_ADDR_LEN];
   char host[MAX_HOSTNAME_LEN];
   
   /* initialize the name */
   strncpy(name, "", 1);
  
   /* sanity check */
   if (ip_addr_is_zero(ip))
      return -ENOTHANDLED;

   /*
    * if the entry is already present in the cache
    * return that entry and don't call the real
    * getnameinfo. we want to increase the speed...
    */
   if (resolv_cache_search(ip, name) == ESUCCESS)
      return ESUCCESS;

   /*
    * the user has requested to not resolve the host,
    * but we perform the search in the cache because
    * the passive engine might have intercepted some
    * request. it is resolution for free... ;)
    */
   if (!GBL_OPTIONS->resolve)
      return -ENOTFOUND;
  
   DEBUG_MSG("host_iptoa() for %s", ip_addr_ntoa(ip, tmp));
   
   /* if not found in the cache, prepare struct and resolve it */
   switch (ntohs(ip->addr_type)) {
      case AF_INET:
         sa4 = (struct sockaddr_in *)&ss;
         sa4->sin_family = AF_INET;
         ip_addr_cpy((u_char*)&sa4->sin_addr.s_addr, ip);
      break;
      case AF_INET6:
         sa6 = (struct sockaddr_in6 *)&ss;
         sa6->sin6_family = AF_INET6;
         ip_addr_cpy((u_char*)&sa6->sin6_addr.s6_addr, ip);
      break;
   }

   /* not found or error */
   if (getnameinfo((struct sockaddr *)&ss, sizeof(ss), 
            host, MAX_HOSTNAME_LEN, NULL, 0, NI_NAMEREQD)) {
      /* 
       * insert the "" in the cache so we don't search for
       * non existent hosts every new query.
       */
      resolv_cache_insert(ip, name);
      return -ENOTFOUND;
   } 
 
   /* the host was resolved... */
   strlcpy(name, host, MAX_HOSTNAME_LEN - 1);

   /* insert the result in the cache for later use */
   resolv_cache_insert(ip, host);

   return ESUCCESS;
}

/*
 * search in the cache for an already
 * resolved host
 */

static int resolv_cache_search(struct ip_addr *ip, char *name)
{
   struct resolv_entry *r;
   u_int32 h;

   /* calculate the hash */
   h = fnv_32(ip->addr, ntohs(ip->addr_len)) & TABMASK;
      
   SLIST_FOREACH(r, &resolv_cache_head[h], next) {
      if (!ip_addr_cmp(&r->ip, ip)) {
         /* found in the cache */
         
         DEBUG_MSG("DNS cache_search: found: %s", r->hostname);
         
         strlcpy(name, r->hostname, MAX_HOSTNAME_LEN - 1);
         return ESUCCESS;
      }
   }
   
   /* cache miss */
   return -ENOTFOUND;
}

/*
 * insert an entry in the cache
 */

void resolv_cache_insert(struct ip_addr *ip, char *name)
{
   struct resolv_entry *r;
   u_int32 h;

   /* calculate the hash */
   h = fnv_32(ip->addr, ntohs(ip->addr_len)) & TABMASK;

   /* 
    * search if it is already in the cache.
    * this will pervent passive insertion to overwrite
    * previous cached results
    */
   SLIST_FOREACH(r, &resolv_cache_head[h], next) {
      /* found in the cache skip it */
      if (!ip_addr_cmp(&r->ip, ip))
         return; 
   }
   
   SAFE_CALLOC(r, 1, sizeof(struct resolv_entry));

   memcpy(&r->ip, ip, sizeof(struct ip_addr));
   r->hostname = strdup(name);
   
   SLIST_INSERT_HEAD(&(resolv_cache_head[h]), r, next);

   DEBUG_MSG("DNS cache_insert: %s", r->hostname);
}

/* EOF */

// vim:ts=3:expandtab

