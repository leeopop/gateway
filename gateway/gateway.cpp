/*
 * gateway.cpp
 *
 *  Created on: 2014. 1. 10.
 *      Author: leeopop
 */

#include <iostream>
#include <rte_eal.h>

using namespace std;

int main(int argc, char** argv)
{
	int ret = rte_eal_init(argc, argv);
	cout<<ret<<endl;
	return 0;
}
