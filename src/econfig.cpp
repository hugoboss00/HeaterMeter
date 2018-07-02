//#include <util/atomic.h>
#include "econfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>


HMConfig::HMConfig()
{
	memset(eeprom_config, 0, sizeof(eeprom_config));
	fd_config = open(CONFIG_FILE, O_RDWR | O_CREAT | O_SYNC, S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP);
	if (fd_config <= 0)
	{
		perror("open config file:");
		return;
	}
	if (read(fd_config, eeprom_config, CONFIG_FILESIZE) != CONFIG_FILESIZE)
	{
		printf("CLEAR Config File\n");
		lseek(fd_config, 0, SEEK_SET);
		if (write(fd_config, eeprom_config, CONFIG_FILESIZE) != CONFIG_FILESIZE)
		{
			perror("init config file:");
			return;
		}
	}
}

uint8_t HMConfig::econfig_read_byte(const void *_src)
{
	unsigned long ofs = (unsigned long)_src;
	if (ofs < CONFIG_FILESIZE)
		return eeprom_config[ofs];
	else
		printf("Error reading offset %ld\n",ofs);
	
	return 0;
}

void HMConfig::econfig_write_byte(void *_dst, uint8_t val)
{
	unsigned long ofs = (unsigned long)_dst;
	if (ofs < CONFIG_FILESIZE)
	{
		eeprom_config[ofs] = val;
		lseek(fd_config, ofs, SEEK_SET);
		if (write(fd_config, &eeprom_config[ofs], 1) != 1)
		{
			perror("write config byte:");
			return;
		}
	}
	else
		printf("Error writing offset %ld\n",ofs);
}


void HMConfig::econfig_read_block(void *_dst, const void *_src, uint8_t n)
{
  const uint8_t *s = (const uint8_t *)_src;
  uint8_t *d = (uint8_t *)_dst;

  while (n--)
    *d++ = econfig_read_byte(s++);
}

unsigned int HMConfig::econfig_read_word(const void *_src)
{
  unsigned int retVal;
  econfig_read_block(&retVal, _src, sizeof(retVal));
  return retVal;
}


void HMConfig::econfig_write_block(const void *_src, void *_dst, uint8_t n)
{
  const uint8_t *s = (uint8_t *)_src;
  uint8_t *d = (uint8_t *)_dst;

  while (n--)
    econfig_write_byte(d++, *s++);
}

void HMConfig::econfig_write_word(void *_dst, uint16_t val)
{
  uint16_t lval = val;
  econfig_write_block(&lval, _dst, sizeof(lval));
}
