#include "common.h"

static memcached_return memcached_auto(memcached_st *ptr, 
                                       char *verb,
                                       char *key, size_t key_length,
                                       unsigned int offset,
                                       uint64_t *value)
{
  size_t send_length;
  char buffer[MEMCACHED_DEFAULT_COMMAND_SIZE];
  unsigned int server_key;
  uint8_t replicas= 0;
  memcached_return rc[MEMCACHED_MAX_REPLICAS];

  unlikely (key_length == 0)
    return MEMCACHED_NO_KEY_PROVIDED;

  unlikely (ptr->hosts == NULL || ptr->number_of_hosts == 0)
    return MEMCACHED_NO_SERVERS;

  if ((ptr->flags & MEM_VERIFY_KEY) && (memcachd_key_test(&key, &key_length, 1) == MEMCACHED_BAD_KEY_PROVIDED))
    return MEMCACHED_BAD_KEY_PROVIDED;

  server_key= memcached_generate_hash(ptr, key, key_length);

  send_length= snprintf(buffer, MEMCACHED_DEFAULT_COMMAND_SIZE, 
                        "%s %.*s %u\r\n", verb, 
                        (int)key_length, key,
                        offset);
  unlikely (send_length >= MEMCACHED_DEFAULT_COMMAND_SIZE)
    return MEMCACHED_WRITE_FAILURE;

  do 
  {
    rc[replicas]= memcached_do(&ptr->hosts[server_key], buffer, send_length, 1);
    if (rc[replicas] != MEMCACHED_SUCCESS)
      goto error;

    rc[replicas]= memcached_response(&ptr->hosts[server_key], buffer, MEMCACHED_DEFAULT_COMMAND_SIZE, NULL);

    /* 
      So why recheck responce? Because the protocol is brain dead :)
      The number returned might end up equaling one of the string 
      values. Less chance of a mistake with strncmp() so we will 
      use it. We still called memcached_response() though since it
      worked its magic for non-blocking IO.
    */
    if (!strncmp(buffer, "ERROR\r\n", 7))
    {
      *value= 0;
      rc[replicas]= MEMCACHED_PROTOCOL_ERROR;
    }
    else if (!strncmp(buffer, "NOT_FOUND\r\n", 11))
    {
      *value= 0;
      rc[replicas]= MEMCACHED_NOTFOUND;
    }
    else
    {
      *value= (uint64_t)strtoll(buffer, (char **)NULL, 10);
      rc[replicas]= MEMCACHED_SUCCESS;
    }
    /* On error we just jump to the next potential server */
error:
    if (replicas > 1 && ptr->distribution == MEMCACHED_DISTRIBUTION_CONSISTENT)
    {
      if (server_key == (ptr->number_of_hosts - 1))
        server_key= 0;
      else
        server_key++;
    }
  } while ((++replicas) < ptr->number_of_replicas);

  /* As long as one object gets stored, we count this as a success */
  while (replicas--)
  {
    if (rc[replicas] == MEMCACHED_STORED)
      return MEMCACHED_SUCCESS;
  }

  return rc[0];
}

memcached_return memcached_increment(memcached_st *ptr, 
                                     char *key, size_t key_length,
                                     uint32_t offset,
                                     uint64_t *value)
{
  memcached_return rc;

  LIBMEMCACHED_MEMCACHED_INCREMENT_START();
  rc= memcached_auto(ptr, "incr", key, key_length, offset, value);
  LIBMEMCACHED_MEMCACHED_INCREMENT_END();

  return rc;
}

memcached_return memcached_decrement(memcached_st *ptr, 
                                     char *key, size_t key_length,
                                     uint32_t offset,
                                     uint64_t *value)
{
  memcached_return rc;

  LIBMEMCACHED_MEMCACHED_DECREMENT_START();
  rc= memcached_auto(ptr, "decr", key, key_length, offset, value);
  LIBMEMCACHED_MEMCACHED_DECREMENT_END();

  return rc;
}
