#! /bin/sh

# client: git archive
cd $HOME/git/rftp/netkit-ftp-0.17
git archive --prefix=rftp-0.10/ v0.10 | gzip -9 > ../rftp-0.10.tar.gz

# server: git archive
cd $HOME/git/rftp/linux-ftpd-0.17
git archive --format=tar --prefix=rftpd-0.10/ HEAD | gzip -9 > ../rftpd-0.10.tar.gz
