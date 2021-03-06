#! /bin/sh

GITSRCDIR=$HOME/git/rftp
RPMDIR=$HOME/rftpbuild
VERSION=0.15
RELEASE=2

TAG=v$VERSION-$RELEASE

for subdir in BUILD RPMS SOURCES SPECS SRPMS
do
	test -d $RPMDIR/$subdir || mkdir -p $RPMDIR/$subdir
done

# clean
rm -rf $RPMDIR/BUILD/*

# cp configuration file
cp $GITSRCDIR/conf/rcftp.spec $RPMDIR/SPECS/
cp $GITSRCDIR/conf/rftpd.spec $RPMDIR/SPECS/

# client: git archive
cd $GITSRCDIR/netkit-ftp
# HEAD or vx.xx
git archive --format=tar --prefix=rcftp-$VERSION/ $TAG | gzip -9 > $RPMDIR/SOURCES/rcftp-$VERSION-$RELEASE.tar.gz


# build client
cd $RPMDIR/SPECS/
rpmbuild -ba rcftp.spec

# server: git archive
cd $GITSRCDIR/linux-ftpd
git archive --format=tar --prefix=rftpd-$VERSION/ $TAG | gzip -9 > $RPMDIR/SOURCES/rftpd-$VERSION-$RELEASE.tar.gz

# build server
cd $RPMDIR/SPECS/
rpmbuild -ba rftpd.spec
