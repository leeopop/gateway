/*
 * util.cpp
 *
 *  Created on: 2014. 1. 10.
 *      Author: leeopop
 */


#include "util.hh"

bool string_to_mac(const std::string &mac, char* ret)
{

	int lowbit = 0;
	int highbit = 0;

	bool isHighDone = 0;
	int current_place = 0;

	for(int k=0; k<mac.length(); k++)
	{
		int val = hex_to_int(mac[k]);
		if(val == -1)
			continue;

		if(isHighDone)
		{
			if(current_place == 6)
				return false;

			isHighDone = false;
			lowbit = val;
			ret[current_place++] = (highbit << 4) + lowbit;
			highbit = 0;
			lowbit = 0;
		}
		else
		{
			highbit = val;
			isHighDone = true;
		}
	}

	return !isHighDone;
}

int hex_to_int(char val)
{
	if(val >= 'A' && val <= 'F')
		return 10 + (val-'A');
	if(val >= 'a' && val <= 'f')
		return 10 + (val-'a');
	if(val >= '0' && val <= '9')
		return (val-'0');
	else
		return -1;
}
