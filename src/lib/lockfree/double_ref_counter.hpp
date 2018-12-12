#ifndef LOCKFREE_DOUBLE_REF_COUNTER_H_INCLUDED
#define LOCKFREE_DOUBLE_REF_COUNTER_H_INCLUDED

#include <atomic>
#include <utility>
#include <type_traits>

namespace lockfree{

/*
 * An empty struct tag type used to default construct an object
 * inside of a double_ref_counter.
 * 
 * This is necessary to differentiate constructing an empty
 * double_ref_counter from a double_ref_counter containing a
 * default-constructed object.
 */
struct default_construct_t{
	explicit default_construct_t() = default;
};

inline constexpr default_construct_t default_construct{};

/*
 * Double-counting reference counter class.
 *
 * A const double_ref_counter can still have its ex_count modified,
 * but not its internals pointer.  Hence the funky usage of const/mutable.
 */
template <class T>
class double_ref_counter{
public:
	
	//Public Types
	using value_type = T;
	class counted_ptr;
	
	//Default and Internal Default Constructors
	double_ref_counter() : front_end(external_counter{nullptr, 0}) {}
	double_ref_counter(default_construct_t) : front_end(external_counter{new internal_counter(), 0}) {}		//Since the default_construct_t parameter is passed by value, this constructor is selected over the single-parameter forwarding constructor when the parameter is of type default_construct_t.
	
	//Forwarding Constructors
	//Cannot forward only a double_ref_counter.  Otherwise, there would be conflict with the copy and move constructors.  Also implicitly cannot forward only a default_construct_t.
	template <class Arg, class = std::enable_if_t<!std::is_same_v<double_ref_counter, std::remove_cv_t<std::remove_reference_t<Arg>>>>>
	double_ref_counter(Arg&& arg) : front_end(external_counter{new internal_counter(std::forward<Arg>(arg)), 0}) {}
	template <class... Args, class = std::enable_if_t<sizeof...(Args) >= 2>>
	double_ref_counter(Args&&... args) : front_end(external_counter{new internal_counter(std::forward<Args>(args)...), 0}) {}
	
	//Copy/Move Constructors and Destructor
	double_ref_counter(const double_ref_counter& other);
	double_ref_counter(double_ref_counter&& other);
	~double_ref_counter() {erase();}
	
	//Assignment Operators
	double_ref_counter& operator=(const double_ref_counter& other);
	double_ref_counter& operator=(double_ref_counter&& other);
	
	//Member Functions
	counted_ptr obtain() const;
	template <class... Args> void replace(Args&&... args);
	template <class... Args> bool try_replace(const counted_ptr& expected, Args&&... args);
	void erase();
	
private:
	
	//Private Types
	class internal_counter;
	struct external_counter{
		internal_counter* internals;
		unsigned int ex_count;
	};
	
	//Data Members
	mutable std::atomic<external_counter> front_end;
	
};

template <class T>
double_ref_counter<T>::double_ref_counter(const double_ref_counter& other) : front_end(external_counter{nullptr, 0}){
	*this = other;
}

template <class T>
double_ref_counter<T>::double_ref_counter(double_ref_counter&& other) : front_end(external_counter{nullptr, 0}){
	*this = std::move(other);
}

template <class T>
double_ref_counter<T>& double_ref_counter<T>::operator=(const double_ref_counter& other){
	counted_ptr other_ref = other.obtain();		//This call to obtain ensures that the other counter's internal_counter won't fall out of scope while copying.
	if(other_ref.counted_internals != nullptr){
		other_ref.counted_internals->attach();
	}
	
	external_counter old_front_end = front_end.load();		//memory order?
	while(!front_end.compare_exchange_weak(old_front_end, external_counter{other_ref.counted_internals, 0})){}		//memory order?
	if(old_front_end.internals != nullptr){
		old_front_end.internals->detach(old_front_end.ex_count);
	}
	return *this;
}

template <class T>
double_ref_counter<T>& double_ref_counter<T>::operator=(double_ref_counter&& other){
	external_counter other_front_end = other.front_end.load();	//memory order?
	while(!other.front_end.compare_exchange_weak(other_front_end, external_counter{nullptr, 0})){}	//memory order?
	
	external_counter old_front_end = front_end.load();		//memory order?
	while(!front_end.compare_exchange_weak(old_front_end, other_front_end)){}		//memory order?
	if(old_front_end.internals != nullptr){
		old_front_end.internals->detach(old_front_end.ex_count);
	}
	return *this;
}

template <class T>
typename double_ref_counter<T>::counted_ptr double_ref_counter<T>::obtain() const{
	external_counter old_front_end = front_end.load(), new_front_end;		//memory order?
	do{
		new_front_end = old_front_end;	//Note that if CAS fails below, old_front_end will change to the actual value.
		++(new_front_end.ex_count);
	}while(!front_end.compare_exchange_weak(old_front_end, new_front_end));	//memory order? We only need weak CAS here, since we just repeat.
	return counted_ptr(new_front_end.internals);
}

template <class T>
template <class... Args>
void double_ref_counter<T>::replace(Args&&... args){
	external_counter old_front_end = front_end.load(), new_front_end{new internal_counter(std::forward<Args>(args)...), 0};	//memory order?
	while(!front_end.compare_exchange_weak(old_front_end, new_front_end)){}	//need to ensure that the new_front_end was actually initialized before this CAS, memory order?
	if(old_front_end.internals != nullptr){
		old_front_end.internals->detach(old_front_end.ex_count);
	}
}

template <class T>
template <class... Args>
bool double_ref_counter<T>::try_replace(const counted_ptr& expected, Args&&... args){
	external_counter old_front_end = front_end.load();	//memory order?
	if(old_front_end.internals != expected.counted_internals){
		return false;
	}
	
	external_counter new_front_end{new internal_counter(std::forward<Args>(args)...), 0};
	while(!front_end.compare_exchange_weak(old_front_end, new_front_end)){	//memory order?
		if(old_front_end.internals != expected.counted_internals){
			delete new_front_end.internals;
			return false;
		}
	}
	if(old_front_end.internals != nullptr){
		old_front_end.internals->detach(old_front_end.ex_count);
	}
	return true;
}

template <class T>
void double_ref_counter<T>::erase(){
	external_counter old_front_end = front_end.load();		//memory order?
	while(!front_end.compare_exchange_weak(old_front_end, external_counter{nullptr, 0})){}	//memory order?
	if(old_front_end.internals != nullptr){
		old_front_end.internals->detach(old_front_end.ex_count);
	}
}

/*
 * The internal counter for the double-counting reference counter.
 */
template <class T>
class double_ref_counter<T>::internal_counter{
public:
	
	//Constructors/Destructor
	template <class... Args> internal_counter(Args&&... args) : data(std::forward<Args>(args)...), counters(internal_counts{1, 0}) {}
	internal_counter(const internal_counter&) = delete;
	internal_counter(internal_counter&&) = delete;
	~internal_counter() = default;	//Destructor doesn't need to do anything.
	
	//Assignment Operators
	internal_counter& operator=(const internal_counter&) = delete;
	internal_counter& operator=(internal_counter&&) = delete;
	
	//Observer Functions
	void release();
	
	//Referrer Functions
	void attach();
	void detach(unsigned int observers);
	
private:
	
	friend double_ref_counter<T>::counted_ptr;
	
	//Private Types
	struct internal_counts{
		unsigned int referrers;
		int in_count;
	};
	
	//Data Members
	value_type data;
	std::atomic<internal_counts> counters;
	
};

template <class T>
void double_ref_counter<T>::internal_counter::release(){
	internal_counts old_counters = counters.load(), new_counters;	//memory order?
	do{
		new_counters = old_counters;
		++(new_counters.in_count);
	}while(!counters.compare_exchange_weak(old_counters, new_counters));	//memory order?
	if(new_counters.referrers == 0 && new_counters.in_count == 0){
		delete this;
	}
}

template <class T>
void double_ref_counter<T>::internal_counter::attach(){
	internal_counts old_counters = counters.load(), new_counters;	//memory order?
	do{
		new_counters = old_counters;
		++(new_counters.referrers);
	}while(!counters.compare_exchange_weak(old_counters, new_counters));	//memory order?
}

template <class T>
void double_ref_counter<T>::internal_counter::detach(unsigned int observers){
	internal_counts old_counters = counters.load(), new_counters;	//memory order?
	do{
		new_counters = old_counters;
		--(new_counters.referrers);
		new_counters.in_count -= observers;
	}while(!counters.compare_exchange_weak(old_counters, new_counters));	//memory order?
	if(new_counters.referrers == 0 && new_counters.in_count == 0){
		delete this;
	}
}

/*
 * The RAII class protecting access to an internal counter object.
 * This object is not thread-safe, and should not be shared between threads.
 */
template <class T>
class double_ref_counter<T>::counted_ptr{
public:
	
	//Constructors/Destructor
	counted_ptr(internal_counter* ptr = nullptr) : counted_internals(ptr) {}
	counted_ptr(const counted_ptr&) = delete;
	counted_ptr(counted_ptr&& other) : counted_internals(other.counted_internals) {other.counted_internals = nullptr;}
	~counted_ptr() {if(counted_internals != nullptr){counted_internals->release();}}
	
	//Assignment Operators
	counted_ptr& operator=(const counted_ptr&) = delete;
	counted_ptr& operator=(counted_ptr&& other);
	
	//Properties Functions
	bool has_data() const {return counted_internals != nullptr;}
	
	//Const Accessors
	//Can throw nullptr exceptions (so can the Mutable Accessors).
	std::add_const_t<value_type>& operator*() const {return counted_internals->data;}
	std::add_const_t<value_type>* operator->() const {return &(counted_internals->data);}
	
	//Mutable Accessors
	//Disabled if value_type is const (with SFINAE).  Mutating data with these functions is not thread-safe unless the underlying data structure is thread-safe.
	template <class = std::enable_if_t<std::is_same_v<value_type, std::remove_const_t<value_type>>>> value_type& operator*() {return counted_internals->data;}
	template <class = std::enable_if_t<std::is_same_v<value_type, std::remove_const_t<value_type>>>> value_type* operator->() {return &(counted_internals->data);}
	
private:
	
	friend double_ref_counter<T>;
	
	//Data Members
	internal_counter* counted_internals;
	
};

template <class T>
typename double_ref_counter<T>::counted_ptr& double_ref_counter<T>::counted_ptr::operator=(counted_ptr&& other){
	if(counted_internals != nullptr){
		counted_internals->release();
	}
	counted_internals = other.counted_internals;
	other.counted_internals = nullptr;
	return *this;
}

}

#endif