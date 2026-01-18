# udmabuf-freebsd-test

Test suite for `udmabuf` on FreeBSD using `atf` (Automated Testing Framework) and `kyua`.
it contains some simple udmabuf test and a selftest ported from linux

## Prerequisites

Ensure you have a C compiler and the `kyua` `atf` testing framework installed.

```sh
pkg install kyua atf
```

## Build Instructions

Navigate to the `tests` directory and run `make` to compile the test programs.

```sh
cd tests
make SYSDIR=/path/to/freebsd-src/sys DRMKMOD_DIR=/path/to/drm-kmod
```

## Running Tests

You can run the tests using `kyua`.

1.  **Run tests**:
    ```sh
    kyua test
    ```

2.  **View report**:
    ```sh
    kyua report
    ```

3.  **Debug a specific test case** (optional):
    If you need to debug a specific test case, you can run it directly:
    ```sh
    ./udmabuf_test -r <test_case_name>
    ```
    Or use `kyua debug`:
    ```sh
    kyua debug <test_program>:<test_case_name>
    ```

## Cleaning Up

To clean up the build artifacts:

```sh
make clean
```

## TODO
now the case is
```
kyua test
udmabuf_test:basic_info  ->  skipped: Requires root privileges  [0.000s]
udmabuf_test:open_close  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:create_list_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:huge_table_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:huge_table_pin_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:memfd_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:normal_case_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:offset_align_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]
udmabuf_test_linux:size_align_check  ->  skipped: Required kmods are not loaded: udmabuf.  [0.001s]

Results file id is home_yzs_freebsd_udmabuf-freebsd-test_tests.20260118-121018-954444
Results saved to /home/yzs/.kyua/store/results.home_yzs_freebsd_udmabuf-freebsd-test_tests.20260118-121018-954444.db

0/9 passed (0 broken, 0 failed, 9 skipped)
```