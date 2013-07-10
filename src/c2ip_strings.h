#ifndef __C2IP_STRINGS_H__L1IPQZCXE6__
#define __C2IP_STRINGS_H__L1IPQZCXE6__

typedef struct C2IPStringMap C2IPStringMap;

struct C2IPStringMap
{
  unsigned int id;
  const char *str;
};

extern const unsigned int c2ip_funtion_name_map_length;
extern const C2IPStringMap c2ip_funtion_name_map[];

const char *
c2ip_string_map(const C2IPStringMap *map, unsigned int length, unsigned int id);
#endif /* __C2IP_STRINGS_H__L1IPQZCXE6__ */
