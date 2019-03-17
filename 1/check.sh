python generator.py -f test1.txt -c 10000 -m 10000
python generator.py -f test2.txt -c 10000 -m 10000
python generator.py -f test3.txt -c 10000 -m 10000
python generator.py -f test4.txt -c 10000 -m 10000
python generator.py -f test5.txt -c 10000 -m 10000
python generator.py -f test6.txt -c 10000 -m 10000
gcc main.c
./a.out test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt
python checker.py -f out.txt
