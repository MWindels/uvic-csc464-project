#ifndef LOCKFREE_HASH_TABLE_H_INCLUDED
#define LOCKFREE_HASH_TABLE_H_INCLUDED

#include <cmath>
#include <atomic>
#include <cstddef>
#include <utility>
#include <cassert>
#include <functional>
#include "double_ref_counter.hpp"

namespace lockfree{

/*
 * A thread-safe lockfree hash table data structure.
 */
template <class K, class V, class Hash = std::hash<K>>
class hash_table{
public:
	
	//Public Types
	using size_type = std::size_t;
	using key_type = K;
	using value_type = V;
	using hasher = Hash;
	
	//Constructors/Destructor
	hash_table(int s = 1) : definitive_table(0, s >= 1 ? s : 1, 0) {}
	hash_table(const hash_table& other) : definitive_table(other.definitive_table) {}	//Shallow copy.
	hash_table(hash_table&& other) : definitive_table(std::move(other.definitive_table)) {}
	~hash_table() = default;
	
	//Assignment Operators
	hash_table& operator=(const hash_table& other) {definitive_table = other.definitive_table; return *this;}	//Likewise.
	hash_table& operator=(hash_table&& other) {definitive_table = std::move(other.definitive_table); return *this;}
	
	//Member Functions
	//All of these assume that the definitive table exists.
	value_type get(const key_type& key) const {return definitive_table.obtain()->get(key);}	//RVO assures move elision.
	void set(const key_type& key, const value_type& value) {definitive_table.obtain()->set(key, value);}
	void remove(const key_type& key) {definitive_table.obtain()->remove(key);}
	
private:
	
	//Private Types
	class table;
	
	//Data Members
	double_ref_counter<table> definitive_table;
	
};

/*
 * The actual data structure which contains key-value pairs.
 * Meant to be used as a component of the hash_table object.
 */
template <class K, class V, class Hash = std::hash<K>>
class hash_table<K, V, Hash>::table{
public:
	
	//Constructors/Destructor
	table() = delete;
	table(int i, int s, int e_c) : id(i), size(s), capacity(int(std::ceil(s * capacity_percentage))), free_cells(capacity - e_c), expected_copies(e_c), hash_function(), next(), cells(new double_ref_counter[s]) {}
	table(const table&) = delete;
	table(table&&) = delete;
	~table() {delete [] cells;}
	
	//Assignment Operators
	table& operator=(const table&) = delete;
	table& operator=(table&&) = delete;
	
	//Member Functions
	value_type get(const key_type& key) const;	//Need to return by value, as the underlying object might fall out of scope after return.
	void set(const key_type& key, const value_type& value);
	void remove(const key_type& key);
	
private:
	
	//Private Types
	struct kv_pair;
	
	//Immutable Data Members
	const int id;
	const int size;
	const int capacity;
	
	//Atomic Data Members
	std::atomic<int> free_cells;
	std::atomic<int> expected_copies;
	
	//Table Data Members
	hasher hash_function;
	double_ref_counter<table> next;
	double_ref_counter<const kv_pair>* const cells;	//This is a const pointer, not a pointer to const data.
	
	//Static Data Members
	static constexpr float capacity_percentage = 0.7;
	static constexpr int resize_factor = 2;
	
	//Private Member Functions
	void copy(const double_ref_counter<const kv_pair>& cell);
	
};

template <class K, class V, class Hash = std::hash<K>>
typename hash_table<K, V, Hash>::value_type hash_table<K, V, Hash>::table::get(const key_type& key) const{
	size_type index = hash_function(key) % size;	//Note that the round-brackets operator for std::hash is const.
	for(size_type i = 0; i < size; ++i){
		double_ref_counter<const kv_pair>::counted_ptr cell = cells[(index + i) % size].obtain();
		if(cell.has_data()){
			if(cell->key == key){	//Assumes keys are compared with the == operator!
				if(cell->redirected){	//Need to advance even if it's also a tombstone.
					return next.obtain()->get(key);	//If an element is marked redirected, then the next table implicitly exists.
				}else if(!cell->tombstone){
					return cell->value;
				}
			}
		}else{
			//table does not contain key
		}
	}
	//table does not contain key
}

template <class K, class V, class Hash = std::hash<K>>
void hash_table<K, V, Hash>::table::set(const key_type& key, const value_type& value){
	
}

template <class K, class V, class Hash = std::hash<K>>
void hash_table<K, V, Hash>::table::remove(const key_type& key){
	
}

template <class K, class V, class Hash = std::hash<K>>
void hash_table<K, V, Hash>::table::copy(const double_ref_counter<kv_pair>& cell){
	
}

/*
 * A key-value data structure used to store information about
 * keys and values in the table objects.
 */
template <class K, class V, class Hash = std::hash<K>>
struct hash_table<K, V, Hash>::table::kv_pair{
	
	//Constructors/Destructor
	kv_pair() = delete;
	kv_pair(const key_type& k, const value_type& v, bool t, bool r) : key(k), value(v), tombstone(t), redirected(r) {}
	kv_pair(const kv_pair&) = delete;
	kv_pair(kv_pair&&) = delete;
	~kv_pair() = default;
	
	//Assignment Operators
	kv_pair& operator=(const kv_pair&) = delete;
	kv_pair& operator=(kv_pair&&) = delete;
	
	//Data Members
	key_type key;
	value_type value;
	bool tombstone;
	bool redirected;
	
};

}

#endif