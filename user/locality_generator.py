import sys
import random
import numpy as np

if len(sys.argv) < 3:
    raise ValueError("You must pass the <size> as MB and <percent> for locality. For example: 20 for 80-20 locality and 0 for 100-0 locality")

size = int(sys.argv[1])
percent = int(sys.argv[2])
int_size = 4
random.seed(10)

num_of_int = size * 1024 * 1024 // int_size

output_list = []
helper_list = np.random.randint(10, size=num_of_int)

percent_max_num = (num_of_int * percent) // 100

i = (percent // 10) - 1 # 1 for 20% 2 for 30%....
if i >= 0:
    for index in helper_list:
        if index > i:
            output_list.append(random.randint(0, percent_max_num - 1))
        else:
            output_list.append(random.randint(percent_max_num, num_of_int - 1))
else: # If 100 - 0
    output_list = np.random.randint(num_of_int, size=num_of_int)

with open("input.txt", "w") as text_file:
    text_file.write(" ".join(str(x) for x in output_list))
    text_file.close()
