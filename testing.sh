#!/bin/bash
# this script is intended to be run from within each student's lab directory. It should be copied there.
LABN=lab2

NUM_TESTS=7
CORRECT_TESTS=0

cd src && make
cd ..

declare -a file_list=("loop1.x" "loop2.x" "loop3.x")

mkdir -p test
./cleaning.sh

for inputfile in "${file_list[@]}";
do
	echo "$inputfile"
	value=1
	i=1
	mismatch=0
	cycle=1
	
	while [ $i -eq $value ]
		do	# change "run $cycle" to "go" to run the entire program without stepping through cycle-by-cycle
			echo "run $cycle\n rdump\n mdump 0x10000000 0x100000ff" | timeout 5 ./refsim inputs/${inputfile} > test/reference_state_${inputfile}.txt
			cd src
			echo "run $cycle\n rdump\n mdump 0x10000000 0x100000ff" | timeout 5 ./sim ../inputs/${inputfile} > ../test/actual_state_${inputfile}.txt
			cd .. 
			echo "------------------cycle $cycle---------------" >> test/diff_${inputfile}.txt
			value2=$(echo "$value2" | grep -r 'halted' test/reference_state_${inputfile}.txt)
			value3=$(echo "$value3" | grep -r 'halted' test/actual_state_${inputfile}.txt)
			if [[ -z "${value2// }" ]] && [[ -z "${value3// }" ]] # if length of string is 0
				then
					cycle=$((cycle+1))
				else		
					i=2
			fi
			diff ./dumpsim src/dumpsim >> test/diff_final_${inputfile}.txt
		done
		if [ -s test/diff_final_${inputfile}.txt ] # if the file is not empty
		then
			echo "    FAIL"
		else
			let "CORRECT_TESTS++" # diff file is empty, indicating that the test was passed
		fi
done
echo "Correct tests: $CORRECT_TESTS / $NUM_TESTS"
