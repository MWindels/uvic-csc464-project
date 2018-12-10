#ifndef LOCKING_HASH_TABLE_H_INCLUDED
#define LOCKING_HASH_TABLE_H_INCLUDED

#include <cmath>
#include <mutex>
#include <memory>
#include <functional>
#include <shared_mutex>

namespace locking{

/*
 * A thread-safe locking hash table.
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
	hash_table(size_type s = 1) : mu(), size(s >= 1 ? s : 1), capacity(size_type(std::ceil(size * capacity_percentage))), used_size(0), cells(new std::unique_ptr<kv_pair>[size]) {}
	hash_table(const hash_table&) = delete;	//No copy ctor because we're not comparing the copy constructors of the lockfree and locking hash tables.
	hash_table(hash_table&&) = delete;	//Likewise.
	~hash_table() {delete [] cells;}
	
	//Assignment Operators
	hash_table& operator=(const hash_table&) = delete;
	hash_table& operator=(hash_table&&) = delete;
	
	//Member Functions
	bool get(const key_type& key, value_type& ret_value) const;
	void set(const key_type& key, const value_type& value);
	void remove(const key_type& key);
	
private:
	
	//Private Types
	struct kv_pair;
	
	//Table Data Members
	mutable std::shared_mutex mu;
	size_type size;
	size_type capacity;
	size_type used_size;
	std::unique_ptr<kv_pair>* cells;	//We use unique_ptr object to store null kv_pairs.
	
	//Static Data Members
	static constexpr float capacity_percentage = 0.7;
	static constexpr size_type resize_factor = 2;
	
	//Private Member Functions
	void resize();
	
};

template <class K, class V, class Hash, class Compare>
bool hash_table<K, V, Hash, Compare>::get(const key_type& key, value_type& ret_value) const{
	std::shared_lock lk(mu);	//Gains shared access.
	
	size_type index = hasher()(key) % size;
	for(size_type i = 0; i < size; ++i){
		std::unique_ptr<kv_pair>& cell = cells[(index + i) % size];
		if(cell){
			if(!cell->tombstone){
				if(comparer()(key, cell->key)){
					ret_value = cell->value;	//Assumes a copy constructor exists.
					return true;
				}
			}
		}else{
			return false;
		}
	}
	return false;
}

template <class K, class V, class Hash, class Compare>
void hash_table<K, V, Hash, Compare>::set(const key_type& key, const value_type& value){
	std::unique_lock lk(mu);	//Gains exclusive access.
	
	if(used_size >= capacity){
		resize();
	}
	
	size_type index = hasher()(key) % size;
	for(size_type i = 0; i < size; ++i){
		std::unique_ptr<kv_pair>& cell = cells[(index + i) % size];
		if(cell){
			if(comparer()(key, cell->key)){
				cell->value = value;
				return;
			}
		}else{
			++used_size;
			cells[(index + i) % size] = std::make_unique<kv_pair>(key, value, false);
			return;
		}
	}
}

template <class K, class V, class Hash, class Compare>
void hash_table<K, V, Hash, Compare>::remove(const key_type& key){
	std::unique_lock lk(mu);	//Gains exclusive access.
	
	size_type index = hasher()(key) % size;
	for(size_type i = 0; i < size; ++i){
		std::unique_ptr<kv_pair>& cell = cells[(index + i) % size];
		if(cell){
			if(comparer()(key, cell->key)){
				cell->tombstone = true;
				return;
			}
		}
	}
}

template <class K, class V, class Hash, class Compare>
void hash_table<K, V, Hash, Compare>::resize(){	//Assumes that the resizing thread has already obtained an exclusive lock.
	size_type old_size = size;
	size *= resize_factor;
	capacity = size_type(std::ceil(capacity_percentage * size));
	
	std::unique_ptr<kv_pair>* new_cells = new std::unique_ptr<kv_pair>[size];
	try{
		for(size_type i = 0; i < old_size; ++i){
			std::unique_ptr<kv_pair>& cell = cells[i];
			if(cell){
				if(!cell->tombstone){
					size_type index = hasher()(cell->key) % size;
					for(size_type j = 0; j < size; ++j){
						std::unique_ptr<kv_pair>& new_cell = new_cells[(index + j) % size];
						if(!new_cell){
							new_cells[(index + j) % size] = std::make_unique<kv_pair>(cell->key, cell->value, false);
							break;
						}
					}
				}else{
					--used_size;
				}
			}
		}
	}catch(...){
		delete [] new_cells;
		throw;
	}
	
	delete [] cells;
	cells = new_cells;
}

/*
 * A key-value data structure used to store info about
 * keys and values in a hash_table.
 */
template <class K, class V, class Hash, class Compare>
struct hash_table<K, V, Hash, Compare>::kv_pair{
	
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