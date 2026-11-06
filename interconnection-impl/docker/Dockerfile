FROM secretflow/spu-ci
WORKDIR /home/admin
COPY ic_main ./
COPY data ./data/
CMD ["/home/admin/ic_main", "-disable_handshake=1"]