# L2-Cache-aware-Coscheduling
Abstract:
  Simultaneous multithreading is meant to tolerate limitations in instruction-level parallelism by issuing 
from multiple threads in the same cycle, and by extension of this multi-thread simultaneous issue, lessen 
the effects of long latency events like cache misses and TLB-misses. Thus Simultaneous Multithreaded (SMT) 
processors are emerging as effective ways to utilize the resources of modern superscalar architectures. However, 
the full potential of SMT has not yet been exploited by Linux. Some preliminary considerations were implemented 
in the old O(1) scheduler, but in the CFS scheduler, the only specific consideration given to SMT seems to be 
restricted to its position in the scheduling domains hierarchy when it comes to load balancing and thread migration. 
In this work, we propose an instrumentation and modification to the existing Linux CFS scheduler to use L2 cache miss
counts as an SMT co-schedule criterion without compromising interactivity. We designed and implemented the algorithm 
in Linux kernel v3.12.5. We verified the performance using SPEC CPU2006 integer benchmark programs and using our
handcrafted benchmark OpenMP matrix multiplication program.

The project is contributed by Suhas D Heeraskar under the guidance of Krishna Kumar and Saidalavi Kaladi.

LinkedId links:
Suhas D Heeraskar : https://www.linkedin.com/profile/view?id=AAMAABAKYTMB6Kb3tpz7d8wlqR5ehegf09rsSM8&trk=hp-identity-name
Krishna Kumar : https://www.linkedin.com/profile/view?id=AAkAAABL1qsBsfmi1wfcqdxPdCilWqRLm7Xkt74&authType=NAME_SEARCH&authToken=-NuO&locale=en_US&trk=tyah&trkInfo=clickedVertical%3Amynetwork%2CclickedEntityId%3A4970155%2CauthType%3ANAME_SEARCH%2Cidx%3A1-1-1%2CtarId%3A1442666245617%2Ctas%3Akrishna%20kumar
