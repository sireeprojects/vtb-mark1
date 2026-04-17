qemu-system-x86_64 \
     \
    -name vlm0,debug-threads=on \
    -machine ubuntu,accel=kvm \
    -cpu host \
    -enable-kvm -m 1536 \
    -object memory-backend-file,id=mem,size=1536M,mem-path=/dev/hugepages,share=on \
    -numa node,memdev=mem -mem-prealloc \
    -smp 8,sockets=2,cores=2,threads=2 -no-user-config \
    -nodefaults -rtc base=utc -boot strict=on \
    -device piix3-usb-uhci,id=usb,bus=pci.0,addr=0x1.0x2 \
    -drive file=/grid/vms/ub24.qcow2,if=none,id=drive-virtio-disk0,format=qcow2 \
    -device virtio-blk-pci,scsi=off,bus=pci.0,addr=0x5,drive=drive-virtio-disk0,id=virtio-disk0,bootindex=1 \
    -chardev pty,id=charserial0 -device isa-serial,chardev=charserial0,id=serial0 \
    -device virtio-balloon-pci,id=balloon0,bus=pci.0,addr=0x6 -msg timestamp=on -daemonize \
    \
    -device virtio-vga \
    -vnc :1 \
    -chardev socket,id=mon0,path=/tmp/qemu-monitor.sock,server=on,wait=off \
    -mon chardev=mon0,mode=readline \
    \
   -serial telnet:127.0.0.1:9900,server,nowait \
    \
    -chardev qemu-vdagent,id=ch1,name=vdagent,clipboard=on \
    -device virtio-serial-pci \
    -device virtserialport,chardev=ch1,id=ch1,name=com.redhat.spice.0 \
    \
    -chardev socket,id=char1,path=/tmp/vhost-user.sock,server=on,wait=off \
    -netdev type=vhost-user,id=hostnet1,queues=1,chardev=char1,vhostforce=on \
    -device virtio-net-pci,netdev=hostnet1,mq=on,vectors=4,id=net1,mac="00:60:2f:00:00:01",bus=pci.0,addr=0x7 \
   ;


# vector formula
# vectors = (queues*2) + 2

    #-chardev socket,id=char1,path=/tmp/vhost-user.sock \
    #-netdev type=vhost-user,id=hostnet1,queues=8,chardev=char1,vhostforce=on \
    #-device virtio-net-pci,netdev=hostnet1,mq=on,vectors=18,id=net1,mac="00:60:2f:00:00:01",bus=pci.0,addr=0x7 \


# qemu \
#     -name TestVM-Ubuntu \
#     -machine ubuntu,accel=kvm \
#     -cpu host \
#     -m 8192 \
#     -smp 8 \
#     -drive file=/sw/imgs/vm.qcow2,format=qcow2,if=none,id=hdd \
#     -device virtio-blk-pci,scsi=off,bus=pci.0,addr=0x7,drive=hdd,id=vdisk,bootindex=1 \
#     -vnc :1 \
#     \
#     -chardev socket,id=ctl_sock,path=/tmp/arunp_vlm_cp \
#     -netdev type=vhost-user,id=vhu_port,queues=2,chardev=ctl_sock,vhostforce=on \
#     -device virtio-net-pci,netdev=vhu_port,id=net1,mac="00:60:2f:00:00:02",bus=pci.0,addr=0x6;

# sudo /lan/cva/avip/users/arunp/from13_scratch/qemu/qemu410/bin/qemu-system-x86_64 \
#      \
#     -name cdn_avip_vlm_0,debug-threads=on \
#     -enable-kvm -m 4096 \
#     -object memory-backend-file,id=mem,size=4096M,mem-path=/dev/hugepages,share=on \
#     -numa node,memdev=mem -mem-prealloc \
#     -smp 2,sockets=1,cores=2,threads=1 -no-user-config \
#     -nodefaults -rtc base=utc -boot strict=on \
#     -device piix3-usb-uhci,id=usb,bus=pci.0,addr=0x1.0x2 \
#     -drive file=/lan/cva/avip/users/arunp/from13_scratch/dpdk/learn/cdn_virtio/qcow/qemu_lm.qcow2,if=none,id=drive-virtio-disk0,format=qcow2 \
#     -device virtio-blk-pci,scsi=off,bus=pci.0,addr=0x5,drive=drive-virtio-disk0,id=virtio-disk0,bootindex=1 \
#     -chardev pty,id=charserial0 -device isa-serial,chardev=charserial0,id=serial0 \
#     -device virtio-balloon-pci,id=balloon0,bus=pci.0,addr=0x6 -msg timestamp=on -daemonize \
#     \
#     -monitor unix:/tmp/monitor,server,nowait \
#     -vnc :1000 -vga std \
#     -netdev tap,id=hostnet0,vhost=on,script=qemu-ifup.csh \
#     -device virtio-net-pci,netdev=hostnet0,id=net0,mac="00:60:2f:00:00:01",bus=pci.0,addr=0x3 \
#     -serial telnet:127.0.0.1:9900,server,nowait \
#     \
#     -chardev socket,id=char1,path=/tmp/arunp_vlm_cp \
#     -netdev type=vhost-user,id=hostnet1,queues=2,chardev=char1,vhostforce \
#     -device virtio-net-pci,netdev=hostnet1,mq=on,mrg_rxbuf=on,vectors=6,id=net1,mac="00:60:2f:00:00:02",bus=pci.0,addr=0x7,emulator=30;
