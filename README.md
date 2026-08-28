# NetUMP
Cross-platform Network MIDI 2.0 session initiator/listener endpoint class

This library is a cross-platform (Linux, MacOS, Windows) implementation of Network MIDI 2.0 (aka "MIDI 2.0 over Ethernet") endpoint. The library performs session management, packet generation and reception. The endpoint can be set as a session initiator or as a session listener

**The library requires the host to implement a realtime/high priority thread which must call the _RunSession()_ method every millisecond.** On Windows machine, this can be achieved using a Multimedia Timer, with time resolution set to 1ms. On Linux and MacOS, a CThread instance (see BEBSDK below) can be used (this is also an alternative to the Multimedia Timers on Windows)

The timing thread accuracy is not critical (to be clear, the thread does not need to call _RunSession()_ every 1.0000 millisecond precisely : the library works perfectly if the method is called every 1.1 or 1.2ms). However, it must be noted that Network UMP transmission is directly controlled by this thread, so the timing accuracy and drift of the thread will impact directly the timing of transmitted packets. Incoming packet timestamping accuracy is also directly related to the thread accuracy.

The library uses BEBSDK cross-platform library, available here : https://github.com/bbouchez/BEBSDK

It must be compiled with the same #defines than BEBSDK (see SDK Readme.md for details) in order to define the target.
