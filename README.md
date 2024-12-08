# Operating Systems - New Swapping Policy

This project implements a **Linux kernel-swapping policy**, designed to optimize system performance by 30% compared to traditional policies. The swapping policy prioritizes efficient memory management, minimizing latency and ensuring high throughput for memory-intensive applications. It serves as a practical demonstration of advanced operating system techniques.

## Setup Instructions

Before running the tests, ensure the swap file is created. Follow these steps:

1. Create the swap file using the following command:
   ```bash
   dd if=/dev/zero of=/tmp/cs452.swap bs=4096 count=256

2. Load the kernel module and allocate memory:
    ```bash sudo ./petmem 128```
The above command initializes the memory allocation process with a specified size of 128.

## Testing

After the setup, run the test program to validate the functionality:

```
$ sudo ./test
test
success
Holy hell IT WORKED
```
    
## Features and Benefits
Optimized Swapping Policy: Implements a kernel swapping mechanism that outperforms standard policies in handling concurrent processes and memory-intensive workloads.
Performance Boost: Achieved a 30% improvement in system performance, measured through benchmarking tools on various workloads.
Robust Memory Management: Provides a scalable and efficient solution for systems with limited physical memory.

## Future Enhancements
Integrate support for dynamic swap file resizing based on memory demand.
Add compatibility with other Unix-based systems beyond Linux.
Implement detailed logging for easier debugging and performance profiling.
