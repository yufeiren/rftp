#! /bin/sh

GITSRCDIR=$HOME/git/rftp
RPMDIR=$HOME/rftp
VERSION=0.13

TAG=v$VERSION

# clear
rm -rf $RPMDIR/BUILD/*

# cp configuration file
cp $GITSRCDIR/conf/rftp.spec $RPMDIR/SPECS/
cp $GITSRCDIR/conf/rftpd.spec $RPMDIR/SPECS/

# client: git archive
cd $GITSRCDIR/netkit-ftp
# HEAD or vx.xx
git archive --format=tar --prefix=rcftp-$VERSION/ $TAG | gzip -9 > $RPMDIR/SOURCES/rcftp-$VERSION.tar.gz


# build client
cd $RPMDIR/SPECS/
rpmbuild -ba rftp.spec

# server: git archive
cd $GITSRCDIR/linux-ftpd
git archive --format=tar --prefix=rftpd-$VERSION/ $TAG | gzip -9 > $RPMDIR/SOURCES/rftpd-$VERSION.tar.gz

# build server
cd $RPMDIR/SPECS/
rpmbuild -ba rftpd.spec