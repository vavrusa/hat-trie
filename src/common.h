/*
 * This file is part of hat-trie.
 *
 * Copyright (c) 2011 by Daniel C. Jones <dcjones@cs.washington.edu>
 *
 *
 * Common typedefs, etc.
 *
 */


#ifndef HATTRIE_COMMON_H
#define HATTRIE_COMMON_H

typedef unsigned long value_t;

/* array-hash table initial size */
#ifndef AHTABLE_INIT_SIZE
  #define AHTABLE_INIT_SIZE 4096 /* tweakable for various data sets */
#endif

/* maximum number of keys that may be stored in a bucket before it is burst */
#ifndef TRIE_BUCKET_SIZE
  #define TRIE_BUCKET_SIZE 16384
#endif

/* alphabet size (0xff for full, 0x7f for 7-bit ASCII) */
#ifndef TRIE_MAXCHAR
  #define TRIE_MAXCHAR 0xff
#endif

#endif


