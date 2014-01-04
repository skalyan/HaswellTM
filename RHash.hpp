#ifndef RHASH_HPP
#define RHASH_HPP

#include "FallbackLock.hpp"
#include "TransRegion.hpp"

#include <iostream>
#include <math.h>

using namespace std;

namespace KaLib {

#define ALIGN64 __attribute__ ((aligned (64)))

const float LoadFactor = 0.1;
typedef int KType;

template <typename VType>
class HashEntry {
public:
   HashEntry(int key, VType value) : key_(key), value_(value), next_(NULL)
                                    {                             }
   KType getKey()                   { return key_;                }
   VType getValue()                 { return value_;              }
   void setValue(VType v)           { value_ = v;                 }
   HashEntry<VType>* getNext()      { return next_;               }
   HashEntry<VType>* setNext(HashEntry<VType>* n)   { next_ = n;  }
   unsigned int countNumEntries();

private:
   KType key_;
   VType value_;
   class HashEntry *next_;
   int padding[13];
};

template <typename VType>
unsigned int
HashEntry<VType>::countNumEntries()
{
   unsigned int c = 0;
   for (HashEntry<VType>*p = this; p != NULL; ++c, p = p->next_);
   return c;
}

template <typename VType>
class HashTable {
public:
   typedef bool (*BucketFn) (HashEntry<VType> *);

   HashTable(int size, FallbackLock* fbLock) : nElems_(0), nCollisions_(0),
                                               nUpdates_(0), TxCount_(0),
                                               nFallBacks_(0),
                                               TableLock_(fbLock)
   {
      if (size <= 0) {
         return;
      }
   
      nBuckets_ = ceil(((float)size) * KaLib::LoadFactor);
   
      buckets_ = new HashEntry<VType> *[nBuckets_];
      for (int i = 0; i < nBuckets_; ++i) {
         buckets_[i] = NULL;
      }
   }

   ~HashTable()
   {
      // printStats(std::cout);
      for (int i = 0; i < nBuckets_; ++i) {
         emptyBucket(buckets_[i]);
      }
   
      delete [] buckets_;
      nElems_ = 0;
   }

   bool insert(KType& k, VType& v);
   bool update(KType& k, VType& v);
   bool lookup(const KType& k, VType& v);

   void printStats(std::ostream& os) const;
   unsigned int countNumEntries() const;
   unsigned int getNumOps() const;
   unsigned int numBuckets() const { return nBuckets_; }

private:
   bool applyToAllBuckets(BucketFn bucketFn);

   unsigned int generateHashedKey(const KType& k);

   void emptyBucket(HashEntry<VType>* b)
   {
      while(b != NULL) {
         HashEntry<VType>* t = b;
         b = b->getNext();
         delete t;
      }
   }

   HashEntry<VType>* lookupEntry(HashEntry<VType>* bktHead, const KType& k);
   void insertNewEntry(HashEntry<VType>** bktHead, HashEntry<VType>* e);
   
   HashEntry<VType> **buckets_;   
   unsigned int nBuckets_;
   unsigned int nElems_;
   unsigned int nUpdates_;
   unsigned int nFallBacks_;

   unsigned int nCollisions_;
   unsigned int TxCount_;

   FallbackLock* TableLock_;
};

template <typename VType>
unsigned int
HashTable<VType>::generateHashedKey(const KType& k)
{
   return (k % nBuckets_);
}

template <typename VType>
bool
HashTable<VType>::insert(KType& k, VType& v)
{
   bool status = false;
   int txr = 0;
   unsigned int hkey = generateHashedKey(k);
   HashEntry<VType>* le = NULL;

   if ((le = lookupEntry(buckets_[hkey], k)) == NULL) {
      HashEntry<VType>* e = new HashEntry<VType>(k,v);
      insertNewEntry(&buckets_[hkey], e);
#ifdef HASH_STATS
      ++nElems_;
#endif
      status = true;
   } else {
      le->setValue(v);
#ifdef HASH_STATS
      ++nUpdates_;
#endif
      status = false;
   }

   return status;
}

template <typename VType>
bool
HashTable<VType>::update(KType& k, VType& v)
{
   bool status = false;
   unsigned int hkey = generateHashedKey(k);
   HashEntry<VType>* le = NULL;

   if ((le = lookupEntry(buckets_[hkey], k)) != NULL) {
      le->setValue(v);
#ifdef HASH_STATS
      ++nUpdates_;
#endif
      status = true;
   }

   return status;
}

template <typename VType>
bool
HashTable<VType>::lookup(const KType& k, VType& v)
{
   bool status = false;
   unsigned int hkey = generateHashedKey(k);
   HashEntry<VType>* le = NULL;

   if ((le = lookupEntry(buckets_[hkey], k)) == NULL) {
      return false;
   } else {
      v = le->getValue();
   }
   return true;
}

template <typename VType>
bool
HashTable<VType>::applyToAllBuckets(BucketFn fn)
{
   bool res = true;
   for (int i = 0; i < nBuckets_; ++i) {
      res = res && (*fn)(buckets_[i]);
   }
   return res;
}

template<typename VType>
HashEntry<VType>*
HashTable<VType>::lookupEntry(HashEntry<VType>* bktHead, const KType& k)
{
   for (HashEntry<VType>*bp = bktHead; bp != NULL; bp = bp->getNext()) {
      if (bp->getKey() == k) {
         return bp;
      }
   }
   return NULL;
}

template<typename VType>
void
HashTable<VType>::insertNewEntry(HashEntry<VType>** bktHead, HashEntry<VType>* e)
{
   if (*bktHead == NULL) {
      *bktHead = e;
   } else {
      ++nCollisions_;
      HashEntry<VType>* t = *bktHead;
      for ( ; t->getNext() != NULL; t = t->getNext());
      t->setNext(e);
   }
   e->setNext(NULL);
}

template<typename VType>
unsigned int
HashTable<VType>::countNumEntries() const
{
   unsigned int res = 0; 
   for (int i = 0; i < nBuckets_; ++i) {
      res += buckets_[i]->countNumEntries();
   }
   return res;
}

template<typename VType>
unsigned int
HashTable<VType>::getNumOps() const
{
   return countNumEntries() + nUpdates_;
}

template<typename VType>
void
HashTable<VType>::printStats(std::ostream& os) const
{
   os << " Number of Buckets        : " << nBuckets_ << endl;
   os << " Number of Entries        : " << countNumEntries() << endl;
   os << " Number of Inserts        : " << nElems_ << endl;
   os << " Number of Updates        : " << nUpdates_ << endl;
   os << " Number of Collisions     : " << nCollisions_ << endl;
   os << " Number of Transactions   : " << TxCount_ << endl;
   os << " Number of Operations     : " << nElems_ + nUpdates_ << endl;
}

} // namespace KaLib

#endif // HASH_HPP
