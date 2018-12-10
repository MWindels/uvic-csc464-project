#ifndef LOCKFREE_HASH_TABLE_H_INCLUDED
#define LOCKFREE_HASH_TABLE_H_INCLUDED

#include <cmath>
#include <atomic>
#include <utility>
#include <functional>
#include "double_ref_counter.hpp"

namespace lockfree{

/*
 * A thread-safe lockfree hash table data structure.
 */
template <class K, class V, class Hash = std::hash<K>, class Compare = std::equal_to<K>>
class hash_table{
public:
	
	//Public Types
	using size_type = int;
	using key_type = K;
	using value_type = V;
	using hasher = Hash;
	using comparer = Compare;
	
	//Constructors/Destructor
	hash_table(size_type s = 1) : definitive_table(s >= 1 ? s : 1) {}
	hash_table(const hash_table& other) : definitive_table(other.definitive_table) {}	//Shallow copy.
	hash_table(hash_table&& other) : definitive_table(std::move(other.definitive_table)) {}
	~hash_table() = default;
	
	//Assignment Operators
	hash_table& operator=(const hash_table& other) {definitive_table = other.definitive_table; return *this;}	//Likewise.
	hash_table& operator=(hash_table&& other) {definitive_table = std::move(other.definitive_table); return *this;}
	
	//Member Functions
	bool get(const key_type& key, value_type& ret_value) const;
	void set(const key_type& key, const value_type& value) {generic_set(key, value, false);}
	void remove(const key_type& key) {value_type unused; generic_set(key, unused, true);}
	
private:
	
	//Private Types
	class table;
	
	//Data Members
	double_ref_counter<table> definitive_table;
	
	//Private Member Functions
	void generic_set(const key_type& key, const value_type& value, bool is_tombstone);
	
};

template <class K, class V, class Hash, class Compare>
bool hash_table<K, V, Hash, Compare>::get(const key_type& key, value_type& ret_value) const{
	bool success = false, ret_tombstone = true;
	typename double_ref_counter<table>::counted_ptr tbl = definitive_table.obtain();
	while(tbl.has_data()){
		if(tbl->get(key, ret_value, ret_tombstone)){
			success = !ret_tombstone;	//Always uses the last occurrance of the key as the definitive answer.
		}
		tbl = std::move(tbl->next.obtain());
	}
	return success;
}

template <class K, class V, class Hash, class Compare>
void hash_table<K, V, Hash, Compare>::generic_set(const key_type& key, const value_type& value, bool is_tombstone){
	typename table::set_result result = table::set_result::failure;
	typename double_ref_counter<table>::counted_ptr prev_tbl, tbl = definitive_table.obtain();
	if(!tbl.has_data()){
		if(!definitive_table.try_replace(tbl, 1)){	//Failure implies someone else made it non-null.
			tbl = definitive_table.obtain();
		}
	}
	while(result != table::set_result::insert){	//Update appropriate kv_pairs in each table until an insert.
		if(!tbl.has_data()){
			if(result == table::set_result::failure && !is_tombstone){
				prev_tbl->next.try_replace(tbl, table::resize_factor * prev_tbl->size);	//Again, failure implies someone else made it non-null.
				tbl = prev_tbl->next.obtain();
			}else{
				break;	//If updates occurred but no insert (or if nothing occurred and the pair was a tombstone), its fine to simply terminate.
			}
		}
		
		typename table::set_result current_result = tbl->set(key, value, is_tombstone);
		if(current_result != table::set_result::failure){
			result = current_result;
		}
		
		prev_tbl = std::move(tbl);
		tbl = prev_tbl->next.obtain();
	}
}

/*
 * The actual data structure which contains key-value pairs.
 * Meant to be used as a component of the hash_table object.
 */
template <class K, class V, class Hash, class Compare>
class hash_table<K, V, Hash, Compare>::table{
public:
	
	//Public Types
	enum struct set_result{
		failure,
		update,
		insert,
	};
	
	//Constructors/Destructor
	table() = delete;
	table(size_type s) : size(s), capacity(size_type(std::ceil(s * capacity_percentage))), table_counters(counters{0, 0, false}), next(), cells(new double_ref_counter<const kv_pair>[s]) {}
	table(const table&) = delete;
	table(table&&) = delete;
	~table() {delete [] cells;}
	
	//Assignment Operators
	table& operator=(const table&) = delete;
	table& operator=(table&&) = delete;
	
	//Member Functions
	bool get(const key_type& key, value_type& ret_value, bool& ret_tombstone) const;
	set_result set(const key_type& key, const value_type& value, bool is_tombstone);
	
private:
	
	friend hash_table<K, V, Hash, Compare>;
	
	//Private Types
	struct kv_pair;
	struct counters{
		size_type elements;
		size_type inserters;	//Wrap flag up in here?
		bool resize_flag;
	};
	
	//Immutable Data Members
	const size_type size;
	const size_type capacity;
	
	//Atomic Data Members
	std::atomic<counters> table_counters;
	
	//Table Data Members
	double_ref_counter<table> next;
	double_ref_counter<const kv_pair>* const cells;	//This is a const pointer, not a pointer to const data.
	
	//Static Data Members
	static constexpr float capacity_percentage = 0.7;
	static constexpr size_type resize_factor = 2;
	
	//Private Member Functions
	bool attempt_insert();
	counters complete_insert(bool success);
	
};

template <class K, class V, class Hash, class Compare>
bool hash_table<K, V, Hash, Compare>::table::get(const key_type& key, value_type& ret_value, bool& ret_tombstone) const{
	size_type index = hasher()(key) % size;
	for(size_type i = 0; i < size; ++i){
		typename double_ref_counter<const kv_pair>::counted_ptr cell = cells[(index + i) % size].obtain();
		if(cell.has_data()){
			if(comparer()(cell->key, key)){
				ret_value = cell->value;	//Assumes copy assignment operator exists.
				ret_tombstone = cell->tombstone;
				return true;
			}
		}else{
			return false;
		}
	}
	return false;
}

template <class K, class V, class Hash, class Compare>
typename hash_table<K, V, Hash, Compare>::table::set_result hash_table<K, V, Hash, Compare>::table::set(const key_type& key, const value_type& value, bool is_tombstone){
	bool attempted_insert = false;
	set_result result = set_result::failure;
	size_type index = hasher()(key);
	for(size_type i = 0; i < size; ++i){
		typename double_ref_counter<const kv_pair>::counted_ptr cell = cells[(index + i) % size].obtain();
		if(cell.has_data()){
			if(comparer()(cell->key, key)){	//Keys are the same, attempt to update.
				if(cells[(index + i) % size].try_replace(cell, key, value, is_tombstone)){
					result = set_result::update;
					break;	//Successfully updated!
				}else{
					--i;	//Repeat the process.  Someone else modified the cell.
				}
			}
		}else if(!is_tombstone){	//Empty cell found, attempt an insertion (unless its a tombstone).
			if(!attempted_insert){
				if(!(attempted_insert = attempt_insert())){
					break;
				}
			}
			if(cells[(index + i) % size].try_replace(cell, key, value, false)){
				result = set_result::insert;
				break;	//Successfully inserted!
			}else{
				--i;	//Repeat the process.  Someone else modified the cell.
			}
		}
	}
	if(attempted_insert){	//This is a little brittle, could use an RAII class or a try-catch block.
		complete_insert(result == set_result::insert);	//Return value not used because we're not attempting to resize tables.
	}
	return result;
}

template <class K, class V, class Hash, class Compare>
bool hash_table<K, V, Hash, Compare>::table::attempt_insert(){
	counters old_counters = table_counters.load(), new_counters;	//memory order?
	do{
		new_counters = old_counters;
		if(new_counters.resize_flag){
			return false;
		}
		++(new_counters.inserters);
		new_counters.resize_flag = (new_counters.elements + new_counters.inserters == capacity);
	}while(!table_counters.compare_exchange_weak(old_counters, new_counters));	//memory order?
	return true;
}

template <class K, class V, class Hash, class Compare>
typename hash_table<K, V, Hash, Compare>::table::counters hash_table<K, V, Hash, Compare>::table::complete_insert(bool success){
	counters old_counters = table_counters.load(), new_counters;	//memory order?
	do{
		new_counters = old_counters;
		--(new_counters.inserters);
		if(success){
			++(new_counters.elements);
		}
	}while(!table_counters.compare_exchange_weak(old_counters, new_counters));	//memory order?
	return new_counters;
}

/*
 * A key-value data structure used to store information about
 * keys and values in the table objects.
 */
template <class K, class V, class Hash, class Compare>
struct hash_table<K, V, Hash, Compare>::table::kv_pair{
	
	//Constructors/Destructor
	kv_pair() = delete;
	kv_pair(const key_type& k, const value_type& v, bool t) : key(k), value(v), tombstone(t) {}
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
	
};

}

#endif