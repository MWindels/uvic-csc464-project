#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
#include <functional>
#include "lib/locking/hash_table.hpp"
#include "lib/lockfree/hash_table.hpp"

std::mutex out_mu;

template <class T>
std::ostream& operator<<(std::ostream& out, const std::vector<T>& vec){
	out << '[';
	for(auto i = vec.begin(); i != vec.end(); ++i){
		out << *i;
		if((i + 1) != vec.end()){
			out << ", ";
		}
	}
	out << ']';
	return out;
}

template <class Table, class K, class V>
void getter(K key, const Table& table){
	{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Getter " << key << "] Getting key " << key << "...\n";
	}
	V val;
	if(table.get(key, val)){
		std::unique_lock lk(out_mu);
		std::cout << "\t[Getter " << key << "] Got: " << val << "\n";
	}else{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Getter " << key << "] Got: NOTHING\n";
	}
}

template <class Table, class K, class V>
void setter(K key, Table& table){
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Setter " << key << "] Setting key " << key << "...\n";
	}
	//table.set(key, "SET: " + std::to_string(key));
	table.set(key, V(key, key));
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Setter " << key << "] Set\n";
	}
}

template <class Table, class K, class V>
void remover(K key, Table& table){
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Remover " << key << "] Removing key " << key << "...\n";
	}
	table.remove(key);
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Remover " << key << "] Removed\n";
	}
}

template <class Table, class K, class V>
void test_scenario(int gtrs, int strs, int rmrs){
	Table table(1);
	std::vector<std::thread> getters;
	std::vector<std::thread> setters;
	std::vector<std::thread> removers;
	
	for(int gts = 0, sts = 0, rms = 0; gts < gtrs || sts < strs || rms < rmrs;){
		if(gts < gtrs && sts < strs && rms < rmrs){
			switch(std::rand() % 3){
			case 0:
				getters.push_back(std::thread(getter<Table, K, V>, K(gts++), std::ref(table)));
				break;
			case 1:
				setters.push_back(std::thread(setter<Table, K, V>, K(sts++), std::ref(table)));
				break;
			case 2:
				removers.push_back(std::thread(remover<Table, K, V>, K(rms++), std::ref(table)));
				break;
			}
		}else if(gts < gtrs && sts < strs){
			if(std::rand() % 2){
				getters.push_back(std::thread(getter<Table, K, V>, K(gts++), std::ref(table)));
			}else{
				setters.push_back(std::thread(setter<Table, K, V>, K(sts++), std::ref(table)));
			}
		}else if(gts < gtrs && rms < rmrs){
			if(std::rand() % 2){
				getters.push_back(std::thread(getter<Table, K, V>, K(gts++), std::ref(table)));
			}else{
				removers.push_back(std::thread(remover<Table, K, V>, K(rms++), std::ref(table)));
			}
		}else if(sts < strs && rms < rmrs){
			if(std::rand() % 2){
				setters.push_back(std::thread(setter<Table, K, V>, K(sts++), std::ref(table)));
			}else{
				removers.push_back(std::thread(remover<Table, K, V>, K(rms++), std::ref(table)));
			}
		}else if(gts < gtrs){
			getters.push_back(std::thread(getter<Table, K, V>, K(gts++), std::ref(table)));
		}else if(sts < strs){
			setters.push_back(std::thread(setter<Table, K, V>, K(sts++), std::ref(table)));
		}else if(rms < rmrs){
			removers.push_back(std::thread(remover<Table, K, V>, K(rms++), std::ref(table)));
		}
	}
	
	for(auto i = getters.begin(); i != getters.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	for(auto i = setters.begin(); i != setters.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	for(auto i = removers.begin(); i != removers.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	
	std::cout << "\n\n";
	for(K i = 0; i < strs; ++i){
		V value;
		if(table.get(i, value)){
			std::cout << "(" << i << ", " << value << ")\n";
		}
	}
}

int main(int argc, char* argv[]){
	std::srand(std::time(0));
	
	if(argc < 5){
		std::cerr << "Insufficient arguments:\n\tTry: " << argv[0] << " use_lockfree getters setters remover\n\tIf use_lockfree is 0 the locking hash table is used, otherwise the lockfree hash table is used.\n";
		return -1;
	}
	
	if(std::atoi(argv[1])){
		std::cout << "Using the lockfree hash table...\n\n";
		test_scenario<lockfree::hash_table<int, std::vector<long>>, int, std::vector<long>>(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
	}else{
		std::cout << "Using the locking hash table...\n\n";
		test_scenario<locking::hash_table<int, std::vector<long>>, int, std::vector<long>>(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
	}
	
	return 0;
}