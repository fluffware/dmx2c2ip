#include "c2ip_decode.h"
#include <c2ip.h>
#include <c2ip_strings.h>
#include <string.h>

const char *
access_str(uint8_t flags)
{
  if (flags & C2IP_FLAG_READ_DISABLED) {
    return (flags & C2IP_FLAG_WRITE_DISABLED) ? "--" : "-w";
  } else {
    return (flags & C2IP_FLAG_WRITE_DISABLED) ? "r-" : "rw";
  }
}

const char *
value_type_str(uint8_t type)
{
  switch(type & C2IP_TYPE_MASK) {
  case C2IP_TYPE_U8:
    return "U8";
  case C2IP_TYPE_ENUM:
    return "ENUM";
  case C2IP_TYPE_BOOL:
    return "BOOL";
  case C2IP_TYPE_STRING:
    return "STRING";
  case C2IP_TYPE_S16:
    return "S16";
  case C2IP_TYPE_U16:
    return "U16";
  case C2IP_TYPE_U12:
    return "U12";
  case C2IP_TYPE_FLOAT16:
    return "FLOAT16";
  default:
    return "?";
  }
}

void
value_str(char *buffer, size_t size, uint8_t type, const uint8_t *b)
{
  switch(type & C2IP_TYPE_MASK) {
  case C2IP_TYPE_U8:
    snprintf(buffer, size, "%d", b[1]);
   break;
  case C2IP_TYPE_ENUM:
    snprintf(buffer, size, "%d", b[1]);
   break;
  case C2IP_TYPE_BOOL:
    strncpy(buffer, b[1] ? "TRUE" : "FALSE", size);
    break;
  case C2IP_TYPE_STRING:
    size--;
    if (size > b[0]) size = b[0];
    memcpy(buffer, b+1, size);
    buffer[size] = '\0';
    break;
  case C2IP_TYPE_S16:
    snprintf(buffer, size, "%d", C2IP_S16(b+1));
    break;
  case C2IP_TYPE_U12:
  case C2IP_TYPE_U16:
    snprintf(buffer, size, "%d", C2IP_U16(b+1));
    break;
  case C2IP_TYPE_FLOAT16:
    {
      static const float exp[] = {1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7,
				  1e-8,1e-7,1e-6, 1e-5, 1e-4, 1e-3, 1e-2,1e-1}; 
      uint16_t v = C2IP_U16(b+1);
      float f = ((float)((v>>4) & 0x3ff)) * exp[v&0x0f];
      if (v & 0x8000) f = -f;
      snprintf(buffer, size, "%f", f);
    }
    break;
  default:
    strncpy(buffer, "?", size);
  } 
}




void
c2ip_dump(FILE *file, const uint8_t *packet, unsigned int length)
{
  unsigned int type = C2IP_U16(&packet[0]);
  unsigned int dlen = C2IP_U16(&packet[2]);
  fprintf(file,"T:%d L:%d ",type, dlen);
  switch(type) {
  case C2IP_PKT_TYPE_SETUP:
    switch(packet[4]) {
    case C2IP_REQUEST_AUTH:
      fprintf(file,"Authentication request ");
    case  C2IP_REPLY_AUTH:
      fprintf(file,"Authentication reply ");
      break;
    case C2IP_REQUEST_PING:
      fprintf(file,"Ping request ");
    case  C2IP_REPLY_PING:
      fprintf(file,"Ping reply ");
      break;
      
    }
    break;
  default:
    switch(packet[4]) {
    case C2IP_INDICATION_VALUE:
    case C2IP_REPLY_VALUE:
      {
	unsigned int flags;
	const char *id_str;
	char buffer[257];
	buffer[256] = '\0';
	value_str(buffer, sizeof(buffer), packet[8], &packet[9]);
	id_str = c2ip_string_map(c2ip_funtion_name_map,
				 c2ip_funtion_name_map_length,
				 C2IP_U16(&packet[5]));
	if (!id_str) id_str = "Unknown";
	flags = packet[7];
	fprintf(file,"NS_ID:%d (%s) Access: %s",C2IP_U16(&packet[5]), id_str,
		access_str(flags));
	if (flags & 0x01) fprintf(file, " Has extra info");
	if (packet[8] & C2IP_TYPE_FLAG_RELATIVE) fprintf(file, " Relative change");
	if (flags & ~0x07) fprintf(file, " (Unknown flag bits)");
	fprintf(file, " Type: %s Value: %s",
		value_type_str(packet[8]), buffer);
      }
      break;
    case C2IP_REPLY_FUNC_INFO:
      {
	unsigned int id = C2IP_U16(&packet[5]);
	const char *id_str = c2ip_string_map(c2ip_funtion_name_map,
					     c2ip_funtion_name_map_length, id);
	if (!id_str) id_str = "Unknown";
	fprintf(file,"NS_ID:%d (%s)", id, id_str);
	if (packet[7] == 0x06) {
	  fputs(" Unit: ", file);
	  fwrite(&packet[14], packet[13] , 1, file);
	} else if (packet[7] == 0x03) {
	  const uint8_t *p = &packet[10];
	  unsigned int i = *p++;
	  while(i-- > 0) {
	    fprintf(file, "\n\t%d: ", *p);
	    p++;
	    fwrite(&p[1], *p , 1, file);
	  p += *p + 1;
	  }
	} else {
	  fprintf(file, "Unknown sub type");
	}
      }
      break;
    }
    break;
 
  }
  fputc('\n', file);
}
