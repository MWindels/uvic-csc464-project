#include <mutex>
#include <vector>
#include <thread>
#include <cstdlib>
#include <iostream>
#include <functional>
#include "lib/lockfree/double_ref_counter.hpp"

std::mutex out_mu;

class loud_object{
public:
	
	loud_object() : num(-1) {std::unique_lock lk(out_mu); std::cout << "(-1) Init.\n";}
	loud_object(int i) : num(i) {std::unique_lock lk(out_mu); std::cout << "(" << num << ") Init.\n";}
	loud_object(int i, int j) : num(i + j) {std::unique_lock lk(out_mu); std::cout << "(" << i << "+" << j << "=" << (i + j) << ") Init.\n";}
	loud_object(const loud_object& other) : num(other.num) {std::unique_lock lk(out_mu); std::cout << "(" << num << ") Copy Init.\n";}
	loud_object(loud_object&& other) : num(other.num) {std::unique_lock lk(out_mu); std::cout << "(" << num << ") Move Init.\n";}
	~loud_object() {std::unique_lock lk(out_mu); std::cout << "(" << num << ") Destroyed.\n";}
	
	loud_object& operator=(const loud_object& other) {num = other.num; std::unique_lock lk(out_mu); std::cout << "(" << num << ") Copy Assign.\n"; return *this;}
	loud_object& operator=(loud_object&& other) {num = other.num; std::unique_lock lk(out_mu); std::cout << "(" << num << ") Move Assign.\n"; return *this;}
	
	int get_num() const {return num;}
	void set_num(int i) {num = i;}
	
private:
	
	int num;
	
};

std::ostream& operator<<(std::ostream& out, const loud_object& lo){
	return out << lo.get_num();
}

template <class T>
void worker(int id, const lockfree::double_ref_counter<T>& ref){
	auto ptr = ref.obtain();
	
	if(ptr.has_data()){
		std::unique_lock lk(out_mu);
		std::cout << "\t[Worker " << id << "] " << *ptr << "\n";
	}else{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Worker " << id << "] nullptr\n";
	}
}

template <class T>
void copier(int id, lockfree::double_ref_counter<T>& ref_1, const lockfree::double_ref_counter<T>& ref_2){
	lockfree::double_ref_counter<T> my_ref(ref_2);
	my_ref.replace(id);
	ref_1 = my_ref;
	{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Copier " << id << "] Copied\n";
	}
}

template <class T>
void mover(int id, lockfree::double_ref_counter<T>& ref_1, lockfree::double_ref_counter<T>& ref_2){
	lockfree::double_ref_counter<T> my_ref(std::move(ref_2));
	my_ref.replace(id, 0);
	ref_1 = std::move(my_ref);
	{
		std::unique_lock lk(out_mu);
		std::cout << "\t[Mover " << id << "] Moved\n";
	}
}

template <class T>
void test_scenario(int wrkrs, int cprs, int mvrs){
	lockfree::double_ref_counter<T> the_ref;
	std::vector<lockfree::double_ref_counter<T>> other_refs;
	std::vector<std::thread> workers;
	std::vector<std::thread> copiers;
	std::vector<std::thread> movers;
	
	int len = 1 + std::rand() % 49;
	other_refs.push_back(lockfree::double_ref_counter<T>(lockfree::default_construct));
	for(int i = 1; i < len; ++i){
		other_refs.push_back(lockfree::double_ref_counter<T>());
	}
	
	for(int ws = 0, cs = 0, ms = 0; ws < wrkrs || cs < cprs || ms < mvrs;){
		if(ws < wrkrs && cs < cprs && ms < mvrs){
			switch(std::rand() % 3){
			case 0:
				workers.push_back(std::thread(worker<T>, ws++, std::ref(the_ref)));
				break;
			case 1:
				copiers.push_back(std::thread(copier<T>, cs++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
				break;
			case 2:
				movers.push_back(std::thread(mover<T>, 2 * cprs + ms++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
				break;
			}
		}else if(ws < wrkrs && cs < cprs){
			if(std::rand() % 2){
				workers.push_back(std::thread(worker<T>, ws++, std::ref(the_ref)));
			}else{
				copiers.push_back(std::thread(copier<T>, cs++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
			}
		}else if(ws < wrkrs && ms < mvrs){
			if(std::rand() % 2){
				workers.push_back(std::thread(worker<T>, ws++, std::ref(the_ref)));
			}else{
				movers.push_back(std::thread(mover<T>, 2 * cprs + ms++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
			}
		}else if(cs < cprs && ms < mvrs){
			if(std::rand() % 2){
				copiers.push_back(std::thread(copier<T>, cs++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
			}else{
				movers.push_back(std::thread(mover<T>, 2 * cprs + ms++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
			}
		}else if(ws < wrkrs){
			workers.push_back(std::thread(worker<T>, ws++, std::ref(the_ref)));
		}else if(cs < cprs){
			copiers.push_back(std::thread(copier<T>, cs++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
		}else if(ms < mvrs){
			movers.push_back(std::thread(mover<T>, 2 * cprs + ms++, std::ref(the_ref), std::ref(other_refs[std::rand() % len])));
		}
	}
	
	for(auto i = workers.begin(); i != workers.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	for(auto i = copiers.begin(); i != copiers.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
	for(auto i = movers.begin(); i != movers.end(); ++i){
		if(i->joinable()){
			i->join();
		}
	}
}

int main(int argc, char* argv[]){
	std::srand(std::time(0));
	
	if(argc < 4){
		std::cerr << "Insufficient arguments:\n\tTry: " << argv[0] << " workers copiers movers\n";
		return -1;
	}
	
	test_scenario<const loud_object>(std::atoi(argv[1]), std::atoi(argv[2]), std::atoi(argv[3]));
	return 0;
}