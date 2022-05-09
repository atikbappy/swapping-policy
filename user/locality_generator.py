import sys
import random
import numpy as np

if len(sys.argv) < 2:
    raise ValueError("You must pass the size as MB")

size = int(sys.argv[1])
int_size = 4
random.seed(10)

num_of_int = size * 1024 * 1024 // int_size

output_list = []
helper_list = np.random.randint(10, size=num_of_int)

twenty_percent = (num_of_int * 20) // 100

for index in helper_list:
    if index > 1:
        output_list.append(random.randint(0, twenty_percent - 1))
    else:
        output_list.append(random.randint(twenty_percent, num_of_int - 1))

with open("input.txt", "w") as text_file:
    text_file.write(" ".join(str(x) for x in output_list))
    text_file.close()
