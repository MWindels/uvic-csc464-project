#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
#include <functional>
#include "lib/lockfree/hash_table.hpp"
//#include "lib/locking/hash_table.hpp"

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

template <class T>
void getter(int i, const lockfree::hash_table<int, T>& table){
	{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Getter " << i << "] Getting key " << i << "...\n";
	}
	T val;
	if(table.get(i, val)){
		std::unique_lock lk(out_mu);
		std::cout << "\t[Getter " << i << "] Got: " << val << "\n";
	}else{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Getter " << i << "] Got: NOTHING\n";
	}
}

template <class T>
void setter(int i, lockfree::hash_table<int, T>& table){
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Setter " << i << "] Setting key " << i << "...\n";
	}
	//table.set(i, "SET: " + std::to_string(i));
	table.set(i, T(i, i));
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Setter " << i << "] Set\n";
	}
}

template <class T>
void remover(int i, lockfree::hash_table<int, T>& table){
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Remover " << i << "] Removing key " << i << "...\n";
	}
	table.remove(i);
	{
		std::unique_lock lk(out_mu);
		std::cout << "[Remover " << i << "] Removed\n";
	}
}

template <class T>
void test_scenario(int gtrs, int strs, int rmrs){
	lockfree::hash_table<int, T> table(1);
	std::vector<std::thread> getters;
	std::vector<std::thread> setters;
	std::vector<std::thread> removers;
	
	for(int gts = 0, sts = 0, rms = 0; gts < gtrs || sts < strs || rms < rmrs;){
		if(gts < gtrs && sts < strs && rms < rmrs){
			switch(std::rand() % 3){
			case 0:
				getters.push_back(std::thread(getter<T>, gts++, std::ref(table)));
				break;
			case 1:
				setters.push_back(std::thread(setter<T>, sts++, std::ref(table)));
				break;
			case 2:
				removers.push_back(std::thread(remover<T>, rms++, std::ref(table)));
				break;
			}
		}else if(gts < gtrs && sts < strs){
			if(std::rand() % 2){
				getters.push_back(std::thread(getter<T>, gts++, std::ref(table)));
			}else{
				setters.push_back(std::thread(setter<T>, sts++, std::ref(table)));
			}
		}else if(gts < gtrs && rms < rmrs){
			if(std::rand() % 2){
				getters.push_back(std::thread(getter<T>, gts++, std::ref(table)));
			}else{
				removers.push_back(std::thread(remover<T>, rms++, std::ref(table)));
			}
		}else if(sts < strs && rms < rmrs){
			if(std::rand() % 2){
				setters.push_back(std::thread(setter<T>, sts++, std::ref(table)));
			}else{
				removers.push_back(std::thread(remover<T>, rms++, std::ref(table)));
			}
		}else if(gts < gtrs){
			getters.push_back(std::thread(getter<T>, gts++, std::ref(table)));
		}else if(sts < strs){
			setters.push_back(std::thread(setter<T>, sts++, std::ref(table)));
		}else if(rms < rmrs){
			removers.push_back(std::thread(remover<T>, rms++, std::ref(table)));
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
	for(int i = 0; i < strs; ++i){
		T value;
		if(table.get(i, value)){
			std::cout << "(" << i << ", " << value << ")\n";
		}
	}
}

int main(int argc, char* argv[]){
	std::srand(std::time(0));
	
	if(argc < 4){
		std::cerr << "Insufficient arguments:\n\tTry: " << argv[0] << " getters setters remover\n";
		return -1;
	}
	
	test_scenario<std::vector<int>>(std::atoi(argv[1]), std::atoi(argv[2]), std::atoi(argv[3]));
	
	return 0;
}