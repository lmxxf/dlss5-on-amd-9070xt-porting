#include "nr_native_plan.hpp"
#include <fstream>
#include <cstdio>
int main(int argc,char**argv){auto p=nr::make_native_plan(std::stoul(argv[1]),std::stoul(argv[2]),std::stoi(argv[3])?nr::NetworkModel::Small:nr::NetworkModel::Original);std::ofstream(argv[4])<<p.text;printf("%ux%u model=%s\n",p.width,p.height,p.model.c_str());}
