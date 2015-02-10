<a href="https://scan.coverity.com/projects/4222">
<img alt="Coverity Scan Build Status"
src="https://scan.coverity.com/projects/4222/badge.svg"/>
</a>
# libiorouter
libiorouter is LD_PRELOAD library for caching data and metadata read-requests which is usefull mainly for PHP applications which run in distributed systems to local filesystem (e.g. tmpfs) or persitent storage.

libiorouter does not handle cache invalidation across nodes of distributed system, but invalidates data on open with write access.
