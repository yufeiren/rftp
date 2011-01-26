#! /bin/sh

GITSRCDIR=$HOME/git/rftp
RPMDIR=$HOME/rftp

# clear
rm -rf $RPMDIR/BUILD/*

# cp configuration file
cp $GITSRCDIR/conf/rftp.spec $RPMDIR/SPECS/
cp $GITSRCDIR/conf/rftpd.spec $RPMDIR/SPECS

# client: git archive
cd $GITSRCDIR/netkit-ftp-0.17
git archive --format=tar --prefix=rftp-0.10/ v0.10 | gzip -9 > $RPMDIR/SOURCES/rftp-0.10.tar.gz

# build client
cd $RPMDIR/SPECS/
rpmbuild -ba rftp.spec

# server: git archive
cd $GITSRCDIR/linux-ftpd-0.17
git archive --format=tar --prefix=rftpd-0.10/ HEAD | gzip -9 > $RPMDIR/SOURCES/rftpd-0.10.tar.gz

# build server
cd $RPMDIR/SPECS/
rpmbuild -ba rftpd.spec
