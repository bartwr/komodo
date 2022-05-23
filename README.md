# HFNET testnet

We are testing proposed changes to KMD consensus that will alleviate issues we are facing with unreasonably long gaps in block times.

Credit to @deckersu for all changes. 

# How to Participate 
create HFNET directory
```
mkdir ~/HFNET
```

create `~/HFNET/komodo.conf` with the following contents:
```
rpcuser=some_username
rpcpassword=some_secure_password
rpcport=8882
txindex=1
server=1
daemon=1
rpcworkqueue=256
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
```

download the boostrap and extract it to `~/HFNET`
```
cd ~/HFNET
wget https://bootstrap.dexstats.info/HFNET-bootstrap.tar.gz
tar -zxvf HFNET-bootstrap.tar.gz
```


install dependencies
```
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool ncurses-dev unzip git python bison zlib1g-dev wget libcurl4-gnutls-dev bsdmainutils automake curl
```

clone and compile komodod from my patch-hf22 branch
```
cd
git clone https://github.com/alrighttt/komodo/tree/patch-hf22 alright-kmd
cd alright-kmd
./zcutil/build-no-qt.sh -j$(nproc)
```

start the daemon
```
cd ~/alright-kmd/src
./komodod -notary=dummy -pubkey=<I will provide you a pubkey; dm Alright#0419 or https://keybase.io/alrighttt/>  -whitelistaddress=<address for pubkey provided> -minrelaytxfee=0.000035 -opretmintxfee=0.004 -datadir=/home/user/HFNET -addnode=65.21.77.109 -addnode=95.217.198.157 -debug=hfnet &
```

import the key I provided
```
komodo-cli -datadir=/home/user/HFNET importprivkey <WIF I provide you> "" true 2918310
```


Begin mining 
```
komodo-cli -datadir=/home/user/HFNET setgenerate true 1
```

open port 8880
```
sudo ufw allow 8880 comment HFNET
```
