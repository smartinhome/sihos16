SRC := $(wildcard ../../components/sihos16/wmb_*.cpp)
CXXFLAGS := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -DSIHOS_HOST -I../../components/sihos16

decode_cli: decode_cli.cpp $(SRC) $(wildcard ../../components/sihos16/wmb_*.h)
	$(CXX) $(CXXFLAGS) -o $@ decode_cli.cpp $(SRC) -lcrypto

test: decode_cli
	python3 run_tests.py

clean:
	rm -f decode_cli

.PHONY: test clean
