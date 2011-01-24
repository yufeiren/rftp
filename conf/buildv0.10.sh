#! /bin/sh

# git archieve
cd $HOME/git/rftp/netkit-ftp-0.17
git archive --prefix=rftp-0.10/ v0.10 | gzip -9 > ../rftp-0.10.tar.gz



