FROM secretflow/spu-ci

# 设置工作目录
WORKDIR /home/admin

# 将二进制文件复制到工作目录
COPY beaver_server_main ./
RUN chmod +x beaver_server_main  # 确保二进制文件可执行

# # 生成密钥对
# RUN ./beaver_server_main -gen_key=true > /keys/beaver_keys.txt

# # 提取并解码公钥
# RUN grep -Eo 'base64ed public key:.*' /keys/beaver_keys.txt | \
#     sed -E 's/base64ed public key: (.*)/\1/' | \
#     base64 --decode > public_key.pem

# # 提取并解码私钥
# RUN grep -Eo 'base64ed private key:.*' /keys/beaver_keys.txt | \
#     sed -E 's/base64ed private key: (.*)/\1/' | \
#     base64 --decode > private_key.pem

# # 确保密钥文件的权限正确
# RUN chmod 600 private_key.pem

# 暴露端口
EXPOSE 9449

# 启动 Beaver 服务
# CMD ["/bin/bash", "-c", "./beaver_server_main -port=19449 -server_private_key=private_key.pem"]
# CMD ["/bin/bash"]

CMD ["tail", "-f", "/dev/null"]