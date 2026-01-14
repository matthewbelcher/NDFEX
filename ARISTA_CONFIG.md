# Arista 7150 Switch Configuration

Captured from `<switch-user>@<switch-host>` using `show interfaces status`.

```
Port       Name          Status       Vlan     Duplex Speed  Type         Flags Encapsulation
Et1                      notconnect   1        full   10G    Not Present
Et2                      notconnect   1        full   10G    Not Present
Et3                      connected    1        full   10G    10GBASE-CR
Et4                      connected    1        full   10G    10GBASE-CR
Et5                      notconnect   1        full   10G    Not Present
Et6                      notconnect   1        full   10G    Not Present
Et7        external line notconnect   trunk    full   10G    Not Present
Et8                      notconnect   trunk    full   10G    Not Present
Et9                      notconnect   1        full   10G    Not Present
Et10                     notconnect   1        full   10G    Not Present
Et11                     notconnect   1        full   10G    Not Present
Et12                     notconnect   1        full   10G    Not Present
Et13                     notconnect   1        full   10G    Not Present
Et14                     notconnect   1        full   10G    Not Present
Et15                     connected    1        full   10G    10GBASE-CR
Et16                     connected    1        full   10G    10GBASE-CR
Et17                     notconnect   1        full   10G    Not Present
Et18                     notconnect   1        full   10G    Not Present
Et19                     notconnect   1        full   10G    Not Present
Et20                     notconnect   1        full   10G    Not Present
Et21                     notconnect   1        full   10G    Not Present
Et22                     notconnect   1        full   10G    Not Present
Et23                     notconnect   1        full   10G    Not Present
Et24                     notconnect   1        full   10G    Not Present
Et25                     connected    1        full   10G    10GBASE-CR
Et26                     connected    1        full   10G    10GBASE-CR
Et27                     notconnect   1        full   10G    Not Present
Et28                     notconnect   1        full   10G    Not Present
Et29                     notconnect   1        full   10G    Not Present
Et30                     notconnect   1        full   10G    Not Present
Et31                     notconnect   1        full   10G    Not Present
Et32                     notconnect   1        full   10G    Not Present
Et33                     notconnect   1        full   10G    Not Present
Et34                     notconnect   1        full   10G    Not Present
Et35                     notconnect   1        full   10G    Not Present
Et36                     notconnect   1        full   10G    Not Present
Et37                     notconnect   1        full   10G    Not Present
Et38                     notconnect   1        full   10G    Not Present
Et39                     notconnect   1        full   10G    Not Present
Et40                     notconnect   1        full   10G    Not Present
Et41                     connected    1        full   10G    10GBASE-CR
Et42                     connected    1        full   10G    10GBASE-CR
Et43                     notconnect   1        full   10G    Not Present
Et44                     notconnect   1        full   10G    Not Present
Et45                     notconnect   1        full   10G    Not Present
Et46                     notconnect   1        full   10G    Not Present
Et47                     connected    1        full   10G    10GBASE-CR
Et48                     connected    1        full   10G    10GBASE-CR
Et49                     notconnect   1        full   10G    Not Present
Et50                     notconnect   1        full   10G    Not Present
Et51                     notconnect   1        full   10G    Not Present
Et52                     notconnect   1        full   10G    Not Present
Ma1                      connected    routed   a-full a-1G   10/100/1000
```

Captured from `<switch-user>@<switch-host>` using `show running-config | section interface`.

```
!
interface Ethernet1
!
interface Ethernet2
!
interface Ethernet3
!
interface Ethernet4
!
interface Ethernet5
!
interface Ethernet6
!
interface Ethernet7
   description external line
   switchport access vlan 100
   switchport mode trunk
!
interface Ethernet8
   switchport mode trunk
!
interface Ethernet9
!
interface Ethernet10
!
interface Ethernet11
!
interface Ethernet12
!
interface Ethernet13
!
interface Ethernet14
!
interface Ethernet15
!
interface Ethernet16
!
interface Ethernet17
!
interface Ethernet18
!
interface Ethernet19
!
interface Ethernet20
!
interface Ethernet21
!
interface Ethernet22
!
interface Ethernet23
!
interface Ethernet24
!
interface Ethernet25
!
interface Ethernet26
!
interface Ethernet27
!
interface Ethernet28
!
interface Ethernet29
!
interface Ethernet30
!
interface Ethernet31
!
interface Ethernet32
!
interface Ethernet33
!
interface Ethernet34
!
interface Ethernet35
!
interface Ethernet36
!
interface Ethernet37
!
interface Ethernet38
!
interface Ethernet39
!
interface Ethernet40
!
interface Ethernet41
!
interface Ethernet42
!
interface Ethernet43
!
interface Ethernet44
!
interface Ethernet45
!
interface Ethernet46
!
interface Ethernet47
!
interface Ethernet48
!
interface Ethernet49
!
interface Ethernet50
!
interface Ethernet51
!
interface Ethernet52
!
interface Management1
   ip address dhcp
```

Captured from `<switch-user>@<switch-host>` using `show monitor session 1`.

```
Session 1
------------------------

Source Ports:

  Both:        Et47

Destination Ports:

    Et48 :  active
```

Captured from `<switch-user>@<switch-host>` using `show running-config | section monitor`.

```
!
monitor session 1 source Ethernet47
monitor session 1 destination Ethernet48
```
