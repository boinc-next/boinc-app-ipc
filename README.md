# boinc-app-ipc
IPC manager for BOINC clients that provides event-based I/O for two-way application communication.

## Rationale
BOINC platform projects rely on individual applications for crunching itself. They are downloaded to volunteer's computer and are run by a BOINC client.

The communication between client and applications is done via C struct (`SHARED_MEM`) mapped to disk file (`boinc_mmap_file`) and shared between processes. While fast it presents several significant challenges:
* Loading struct data from disk is dangerous. Any process with same priveleges may potentially exploit C string handling.
* It is not possible to reimplement IPC protocol in any language except C and C++.
* Even with C/C++ this approach is fragile since **any change to said struct will break the protocol**.

This program is a black box that isolates impact from malicious applications and allows the clients to use file-based I/O, a universal Unix communication mechanism.

## How it works
The client (or a prying user) launches the IPC manager with `--mmap-dir` pointed to directory with `boinc_mmap_file`.

boinc-app-ipc uses standard streams as illustrated in the following scheme:
```
                                                API boundary
                                                      |            /------------------------\
                                           /->-1024-byte string->- | Aggregate JSON builder | ->-aggregate response (JSON)->- stdout
    /--->----->----->--\                  /           |            |                        |                 
App   1024-byte string      SHARED_MEM                             |     boinc-app-ipc      |
    \---<-----<-----<--/ (mapped to disk) \           |            |                        |
                                           \-<-1024-byte string-<- | Per-channel queues     | -<-aggregate request  (JSON)-<- stdin
                                                      |            \------------------------/
```

Apps receive XML data, yet both streams contain JSON aggregates: a combined object with messages to/from several channels. IPC manager does the convertion between the two formats and breaks aggregate input into messages to relevant shared memory channels.

For example, the following XML response from app:
```
<current_cpu_time>7.176276e+03</current_cpu_time>
<checkpoint_cpu_time>7.129676e+03</checkpoint_cpu_time>
<fraction_done>1.002963e-01</fraction_done>
```

goes to the stdout as:
```
{"app_status":{"checkpoint_cpu_time":7129.6759999999995,"current_cpu_time":7176.2759999999998,"fraction_done":0.1002963}}
```

### Modes of operation
IPC manager can be launched in three modes:
* **View** - in this mode IPC manager merely peeks outcoming messages, without receiveing them (i.e. clearing outcoming buffer) and thus does not interfere with normal client-app communication.
* **Edit** - enables full-fledged input/output, assumes the role of client in the chain.
* **Manage** - in addition to above, creates and manages *mmap* file itself.

Edit and manage modes are only intended for clients or for debug purposes. Using them manually on a running client/app chain **will** lead to app crashes and work unit computation errors.


## Requirements
* C++14
* boinc-app-api
* glibmm
* libxml++
* JsonCpp

## Installing
```
$ autoreconf -i
$ ./configure
$ make
# make install
```
