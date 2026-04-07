default:
	g++ -O3 -march=native frontend.cpp -o frontend `pkg-config --cflags --libs libdpdk`

run:
	@./frontend -l 3-4 \
      --file-prefix=virtio_frontend \
      --no-telemetry \
      --vdev="net_virtio_user0,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:50" \
      --vdev="net_virtio_user1,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:51" \
      --vdev="net_virtio_user2,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:52" \
      --vdev="net_virtio_user3,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:53" \
      --vdev="net_virtio_user4,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:54" \
      --vdev="net_virtio_user5,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:55" \
      --vdev="net_virtio_user6,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:56" \
      --vdev="net_virtio_user7,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:57"

clean:
	@rm -f backend frontend

      #--vdev="net_virtio_user0,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:50" \
      #--vdev="net_virtio_user1,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:51" \
      #--vdev="net_virtio_user2,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:52" \
      #--vdev="net_virtio_user3,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:53" \
      #--vdev="net_virtio_user4,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:54" \
      #--vdev="net_virtio_user5,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:55" \
      #--vdev="net_virtio_user6,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:56" \
      #--vdev="net_virtio_user7,path=/tmp/vhost-user.sock,server=0,queues=2,mac=00:11:22:33:44:57"
