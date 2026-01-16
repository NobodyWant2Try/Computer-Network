# Computer Network Labs

You can check the document from [here](https://edu.n2sys.cn/#/).

Lab3 was too hard for me, and we can get full score as long as we finish 3 of 4 labs.

## lab 1 myftp

The key is socket programming, and it might be easy if we use pthread.

Bugs I met:

+ Expected packet length was wrong, so Receiver could never get the packet from Sender.
+ sha256 value should be got carefully, because the tests might fail to find some sha256 mistakes.

## lab 2 rtp

Different from lab 1, we must use RTP and provide a reliable data transmission.

Bugs I met:

+ Check the range of sequence number carefully, i.e. the max sequence number is $2^{30} - 1$, rather than $2^{32} - 1$.
+ It might fail to receive message if the window size was too big, so I suggest controlling the max window size(this was a trick, but it seemd that I can still pass all the tests).

## lab 4 ns3

This lab was easy to code as long as we understand the document.

## Some useful pages

+ [pthread programming](https://www.cnblogs.com/caojun97/p/17411101.html)
+ [ns3 tutorial](https://www.cnblogs.com/caojun97/p/17411101.html)
