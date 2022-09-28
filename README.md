# wistron-cpld-code-mgmt
wistron cpld firmware manager

will make files:

1. /etc/cpld-release : contain firmware version to Compare with the version value in /media/cpld-{version}/cpld-release
2. /media/cpld-{version}/cpld-release :	to get running version
3. /media/cpld-{version}/{version}	: to get priority
4. /var/lib/wistron-cpld-code-mgmt/cpld -> /media/cpld-{version}	: to check running version
5. /var/lib/wistron-cpld-code-mgmt/{version}	: for reseting /media/cpld-{version}/{version} after reboot.
6. /var/lib/wistron-cpld-code-mgmt/cpld-release : for reseting files after reboot, as below :
      a). /etc/cpld-release 
      b). /media/cpld-{version}/cpld-release
