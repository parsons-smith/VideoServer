#ifndef _HASH_TABLE_HH
#define _HASH_TABLE_HH

#if defined(__BORLANDC__) || (!defined(USE_LIVE555_BOOLEAN) && defined(_MSC_VER) &&  _MSC_VER >= 1400)
// Use the "bool" type defined by the Borland compiler, and MSVC++ 8.0, Visual Studio 2005 and higher
typedef bool Boolean;
#define False false
#define True true
#else
typedef unsigned char Boolean;
#ifndef __MSHTML_LIBRARY_DEFINED__
#ifndef False
const Boolean False = 0;
#endif
#ifndef True
const Boolean True = 1;
#endif

#endif
#endif

class HashTable {
public:
  virtual ~HashTable();
  
  // The following must be implemented by a particular
  // implementation (subclass):
  static HashTable* create(int keyType);
  
  virtual void* Add(char const* key, void* value) = 0;
  // Returns the old value if different, otherwise 0
  virtual Boolean Remove(char const* key) = 0;
  virtual void* Lookup(char const* key) const = 0;
  // Returns 0 if not found
  virtual unsigned numEntries() const = 0;
  Boolean IsEmpty() const { return numEntries() == 0; }
  
  // Used to iterate through the members of the table:
  class Iterator {
  public:
    // The following must be implemented by a particular
    // implementation (subclass):
    static Iterator* create(HashTable const& hashTable);
    
    virtual ~Iterator();
    
    virtual void* next(char const*& key) = 0; // returns 0 if none
    
  protected:
    Iterator(); // abstract base class
  };
  
  // A shortcut that can be used to successively remove each of
  // the entries in the table (e.g., so that their values can be
  // deleted, if they happen to be pointers to allocated memory).
  void* RemoveNext();
  
  // Returns the first entry in the table.
  // (This is useful for deleting each entry in the table, if the entry's destructor also removes itself from the table.)
  void* getFirst(); 
  
protected:
  HashTable(); // abstract base class
};

// Warning: The following are deliberately the same as in
// Tcl's hash table implementation
int const STRING_HASH_KEYS = 0;
int const ONE_WORD_HASH_KEYS = 1;

#endif
