#include <mutex>
#include <cmath>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <functional>
#include "lib/locking/hash_table.hpp"
#include "lib/lockfree/hash_table.hpp"

using testing_clock = std::chrono::steady_clock;

std::mutex acc_vec_mu;
std::mutex mut_vec_mu;

std::vector<testing_clock::duration::rep> acc_vec;
std::vector<testing_clock::duration::rep> mut_vec;

double avg_vector(std::vector<testing_clock::duration::rep>& vec){
	testing_clock::duration::rep sum = 0;
	for(auto i = vec.begin(); i != vec.end(); ++i){
		sum += *i;
	}
	return double(sum) / double(vec.size());
}

double std_dev_vector(std::vector<testing_clock::duration::rep>& vec){
	double avg = avg_vector(vec);
	double sum = 0;
	for(auto i = vec.begin(); i != vec.end(); ++i){
		sum += std::pow(double(*i) - avg, 2.0);
	}
	return std::sqrt(sum / double(vec.size()));
}

template <class Table, class K, class V>
void accessor(int id, Table& table, int ops){
	std::vector<testing_clock::duration::rep> results;
	
	K key;
	V ret_value;
	for(int i = 0; i < ops; ++i){
		key = K(id * i);
		
		testing_clock::time_point start = testing_clock::now();
		table.get(key, ret_value);
		testing_clock::time_point end = testing_clock::now();
		
		results.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
	}
	
	{
		std::unique_lock lk(acc_vec_mu);
		acc_vec.insert(acc_vec.end(), results.begin(), results.end());
	}
}

template <class Table, class K, class V>
void mutator(int id, Table& table, int ops){
	std::vector<testing_clock::duration::rep> results;
	
	K key;
	V value;
	for(int i = 0; i < ops; ++i){
		key = K(id * i);
		value = V(key);
		
		testing_clock::time_point start = testing_clock::now();
		table.set(key, value);
		testing_clock::time_point end = testing_clock::now();
		
		results.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
	}
	
	{
		std::unique_lock lk(mut_vec_mu);
		mut_vec.insert(mut_vec.end(), results.begin(), results.end());
	}
}

template <class Table, class K, class V>
void test_scenario(int acsrs, int mttrs, int ops_per){
	Table table;
	std::vector<std::thread> accessors;
	std::vector<std::thread> mutators;
	
	for(int as = 0, ms = 0; as < acsrs || ms < mttrs;){
		if(as < acsrs && ms < mttrs){
			if(std::rand() % 2){
				accessors.push_back(std::thread(accessor<Table, K, V>, as++, std::ref(table), ops_per));
			}else{
				mutators.push_back(std::thread(mutator<Table, K, V>, ms++, std::ref(table), ops_per));
			}
		}else if(as < acsrs){
			accessors.push_back(std::thread(accessor<Table, K, V>, as++, std::ref(table), ops_per));
		}else if(ms < mttrs){
			mutators.push_back(std::thread(mutator<Table, K, V>, ms++, std::ref(table), ops_per));
		}
	}
	
	for(auto i = accessors.begin(); i != accessors.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	for(auto i = mutators.begin(); i != mutators.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	
	std::cout << "Accessor Average: " << avg_vector(acc_vec) / 1000.0 << " microseconds\n";
	std::cout << "Accessor Standard Deviation: " << std_dev_vector(acc_vec) / 1000.0 << " microseconds\n\n";
	std::cout << "Mutator Average: " << avg_vector(mut_vec) / 1000.0 << " microseconds\n";
	std::cout << "Mutator Standard Deviation: " << std_dev_vector(mut_vec) / 1000.0 << " microseconds\n";
}

int main(int argc, char* argv[]){
	std::srand(std::time(0));
	
	if(argc < 5){
		std::cerr << "Insufficient arguments:\n\tTry: " << argv[0] << " use_lockfree accessors mutators operations_per_thread\n\tIf use_lockfree is 0 the locking hash table is used, otherwise the lockfree hash table is used.\n";
		return -1;
	}
	
	if(std::atoi(argv[1])){
		std::cout << "Using lockfree hash table...\n\n";
		test_scenario<lockfree::hash_table<std::int32_t, std::int32_t>, std::int32_t, std::int32_t>(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
	}else{
		std::cout << "Using locking hash table...\n\n";
		test_scenario<locking::hash_table<std::int32_t, std::int32_t>, std::int32_t, std::int32_t>(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
	}
	
	return 0;
}