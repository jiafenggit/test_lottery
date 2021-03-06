#include "server.h"

int clisock = -1;
int server::frame = 0;

int server::readconf(const char* file)
{
	string sip = readconfig(file, "net", "servip", "127.0.0.1");
	string sport = readconfig(file,"[net]", "servport", "9999");
    m_ip = const_cast<char*>(sip.c_str());
	m_port = str2num(sport);
	return 0;
}
void server::m_setaddr(struct sockaddr_in &sin) const
{
	// struct sockaddr_in sin;
	memset(&sin, 0 , sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(m_ip);
	sin.sin_port = htons(m_port);
}

void server::m_setparameters() const
{
	int readdr = 1,recvbuf = 128*1024, sendbuf = 128*1024;
	struct linger ling;
	//	ling.l_linger = 0;
	ling.l_onoff = 0;

	setsockopt(m_socket, SOL_SOCKET,SO_REUSEADDR, &readdr, sizeof(int));
	setsockopt(m_socket, SOL_SOCKET,SO_RCVBUF, &recvbuf, sizeof(int));
	setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(int));
	setsockopt(m_socket, SOL_SOCKET, SO_LINGER, (const char *)&ling, sizeof(struct linger));

}

int server::m_bind( struct sockaddr_in& sin) const 
{
	int ret = 0;
	ret = bind(m_socket, (struct sockaddr*)&sin, sizeof(sin));
	if(ret <0)
	{
		cout << "bind error\n";
	}
	return ret;
}

int server::m_listen( int backlog) const 
{
	int ret = 0;
	if((ret = listen(m_socket, backlog)) < 0)
	{
		cout<< "listen error\n";
	}
	return ret;
}

int server::m_accept(struct sockaddr_in* cin) const 
{
	socklen_t socklen = sizeof(cin);
    // int clisock = 0;
    clisock = accept(m_socket, (struct sockaddr*)cin, &socklen);
	if(clisock <0)
	{
		cout << " server accept error\n";
		return -1;
	}
	return clisock;
}

int server::m_tcprecv(int m_socket, char *recvbuf, int len, int timeout)
{
	if(NULL == recvbuf || len <=0 )
		return -1;
	int ret = 0;
	fd_set fds;
	struct timeval interval;
   	memset(recvbuf ,0 ,sizeof(recvbuf));
	FD_ZERO(&fds);
	FD_SET(m_socket, &fds);
    if(timeout < 0)
	{
		ret = select(m_socket+1, &fds, NULL,NULL,NULL);
		if(FD_ISSET(m_socket, &fds))
		{
			ret = recv(m_socket , recvbuf, len, 0);
		}
   	}
	else
	{
		interval.tv_sec = timeout;
		interval.tv_usec = 0;
		ret = select(m_socket +1, &fds, NULL, NULL, &interval);
		if(FD_ISSET(m_socket , &fds))
		{
			ret = recv(m_socket , recvbuf, len, 0);
		}
	}

	return (ret > 0)?ret :-2;
}

int server::m_tcpsend(int m_socket, char *sendbuf, int len) const 
{
	if(NULL == sendbuf || len <=0)
		return -1;
	int ret =0;
	int data_left = len;
	int send_tol = 0;
	while(data_left > 0)
	{
		ret = send(m_socket, sendbuf + send_tol, data_left, 0);
        if(ret < 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			{
				usleep(100000);
				ret = 0;
			}
				
		}
		send_tol += ret;
		data_left = len - send_tol;
   	}
	return send_tol;
}

int server::m_readuser(const string userfile)
{
	ifstream infile(userfile.c_str(), ifstream::in);
	if(!infile)
	{
		cout <<__FILE__<<__LINE__<< ":open file error\n";
		return -1;
	}
	string username, passwd;
	string record;
	while(getline(infile, record))
	{
		istringstream isstream(record);
		isstream >> username >> passwd ;
		userpwd.insert(pair<string, string>(username, passwd));
	}
}

int m_varylogin(char * buf, server* serv)
{
	map<string, string>* userpwd = &(serv->userpwd);
    string content = buf;
	int pos = content.find("|");
	string username = content.substr(0, pos);
	string passwd = content.substr(pos+1);
	bool flag = false;
	for(map<string, string>::iterator mapite = (*userpwd).begin(); mapite != (*userpwd).end(); ++mapite)
	{
		if(mapite->first == username && mapite->second == passwd)
		{
			flag = true;
			break;
		}
	}
	if(flag)
	{
		cout << username << " login success\n";   //登录成功或失败给客户端发消息
		m_loginOK(serv, flag);
	}
	else
	{
		cout << "login error\n";
		m_loginOK(serv, flag);
	}
	return 0;
}

int m_setlottery(char * buf, server* serv)
{
	string s = buf;
	string lotnum = s.substr(0, s.find_first_of("|"));
	string interval = s.substr(s.find_first_of("|")+1);
    serv->lotterynum = str2num(lotnum);
	serv->lotteryinterval = str2num(interval);
	if(serv->lotterynum > 0)
	{
		cout<<"set lotterynum ok\n";
		setLotteryOK(serv);	
	}
}

int m_sendlottery(char* buf, server* serv)
{
	int array[4];
	for(int i=0;i< serv->lotterynum; ++i)
	{
		serv->m_play(array);
	  	sleep(serv->lotteryinterval); //set the time interval about the game
		//show in server terminal
		for(int i=0; i< 4;++i)
			cout << array[i] << " ";
		cout << endl;
		{
			lotterytoclient(array, serv);
		}
	}
	playend(serv);
}


void recvthrdfunc(void *arg)
{
	server *serv = (server*)arg;
	int ret = 0, cmd = 0, len = 0;
	unsigned short  crc;
	char buf[1024];
	char content[256];
	do
	{
		while(1)
		{
			if(clisock < 0)
			{
				continue;
			}
			//paser the data  message
			memset(buf, 0, sizeof(buf));
			memset(content, 0, sizeof(content));
			ret = serv->m_tcprecv(clisock, buf, 2, -1); //recv start data
			if((ret != 2) || ((unsigned char)buf[0] != 0xFF) || ((unsigned char)buf[1] != 0xFF))
			{
				break;
			}
			ret = serv->m_tcprecv(clisock, buf+2, 6, -1);  //recv source addr
			if((ret != 6) || (strcmp(buf+2, "000000")!= 0))
			{
				break;
			}
			ret = serv->m_tcprecv(clisock, buf+8, 6, -1); //recv destination addr
			if((ret != 6)||(strncmp(buf+8, "111111", 6) != 0))
			{
  				break;
			}
			ret = serv->m_tcprecv(clisock, buf+14, 2, -1); //recv cmd
			if(ret != 2)
			{
				break;
			}
			cmd = (unsigned char)buf[14]*256 + (unsigned char)buf[15];	
			ret = serv->m_tcprecv(clisock, buf+16, 2, -1); //recv message num
			ret = serv->m_tcprecv(clisock, buf+18, 2, -1); //the length content
			len = (unsigned char)buf[18]*256 + (unsigned char)buf[19];
			ret = serv->m_tcprecv(clisock, buf+20, len+2, -1);  //the last data
			crc = crc_check2(buf+2, len+18);
			unsigned short crc2 = (unsigned char)buf[21 + len]*256 + (unsigned char)buf[22 + len];
			// cout << crc <<"====" <<crc2 << endl;
			// cout << 22 + len << endl;
			// if(crc != crc2)
			// {
			// 	perror("crc error");
			// 	return ;
			// }
			memcpy(content, buf+20, len);
			switch(cmd)
			{
			case 0x0001:
				m_varylogin(content, serv);
				break;
			case 0x0002:
				m_setlottery(content, serv);
				break;
			case 0x0003:
				m_sendlottery(content, serv);
				break;
			default:
				break;
			}
		}
		if(ret < 0)
		{
			close(clisock);
			clisock = -1;
		}
	
	} while (1);
	cout << "server recv error\n";
}

void server::m_recvthrdstart(server* serv)
{
	thread_create(&m_pid, (void*)recvthrdfunc, serv, 1);
}
//得到按照一定 概率生成的vctor
void server::m_getpocketpoll()
{
	int a1[9] = {1,1,1,1,1,1,1,1,1};
	int a2[12] = {2,2,2,2,2,2,2,2,2,2,2,2};
	int a3[5] = {3,3,3,3,3};
	int a4[4] = {4,4,4,4};
	int a5[8] = {5,5,5,5,5,5,5,5};
	int a6[15] = {6,6,6,6,6,6,6,6,6,6,6,6,6,6,6};
	int a7[7] = {7,7,7,7,7,7,7};
	int a8[13] = {8,8,8,8,8,8,8,8,8,8,8,8,8};
	int a9[6] = {9,9,9,9,9,9};
	int a10[3] = {10,10,10};
	int a11[10] = {11,11,11,11,11,11,11,11,11,11};
	int a12[8] = {12,12,12,12,12,12,12,12};

	vector<int> vvec(100,0);
	memcpy(&vvec[0], &a1[0], 9*sizeof(int));
   	memcpy(&vvec[9], &a2[0], 12*sizeof(int));
	memcpy(&vvec[21], &a3[0], 5*sizeof(int));
	memcpy(&vvec[26], &a4[0], 4*sizeof(int));
	memcpy(&vvec[30], &a5[0], 8*sizeof(int));
	memcpy(&vvec[38], &a6[0], 15*sizeof(int));
	memcpy(&vvec[53], &a7[0], 7*sizeof(int));
	memcpy(&vvec[60], &a8[0], 13*sizeof(int));
	memcpy(&vvec[73], &a9[0], 6*sizeof(int));
	memcpy(&vvec[79], &a10[0], 3*sizeof(int));
	memcpy(&vvec[82], &a11[0], 10*sizeof(int));
	memcpy(&vvec[92], &a12[0], 8*sizeof(int));

    // vvec.assign(&a1[0], &a1[9]);
	// for(int i=0;i != 100; ++i)
	// {
	// 	cout << vvec[i] << " ";
	// }
	// cout<<"\n"<< vvec.size();
	// cout<<"----------"<<endl;
	int t=0, tmp, p;
	srand(unsigned(time(NULL)));
	for(int i = 0; i< 100; ++i)
	{
		t = int(random(0, 101));
		if(vec[t] == 0)
		{
		   vec[t] = vvec[i];
		}
		else
		{
			p = t;
			tmp = vvec[i];
			while(++t && t < 100)
			{
				if(vec[t] == 0)
				{
					vec[t] = tmp;
					break;
				}
				else
					continue;
						
			}
			while(t == 100 && (--p >= 0))
			{
				if(vec[p] == 0)
				{
					vec[p] = tmp;
					break;
				}
				else
					continue;
						
			}
		}
		
	}

	// for(int i=0;i != 100; ++i)
	// {
	// 	cout << vec[i] << " ";
	// }
	// cout<<"\n"<< vec.size();
    // cout << "\n---------------\n";
}

void server::m_play(int* array) const 
{
	//get four number from vector by random
	int pos;
	for(int i=0;i < 4;++i)
	{
		pos = int(random(0, 101));
		while(vec[pos] ==0) //这个bug以后在该
		{
			pos = int(random(0, 101));
		}
		array[i] = vec[pos];
	}

}

int m_loginOK(server *serv, bool flag) 
{
	int cmd = 0x1001;
	char buf[256];
	string login= "";
	if(flag)
	{
		login = "login_OK";
	}
    else
		login = "login_ERROR";
	char *snd = const_cast<char*>(login.c_str());
	int sndlen = strlen(snd);
	memset(buf, 0, 256);
	buf[0] = 0xff;
	buf[1] = 0xff;
	memcpy(buf+2, "111111", 6);
	memcpy(buf+8, "000000", 6);
    buf[14] = cmd / 256;
    buf[15] = cmd % 256;
	if(server::frame >= 65535)
		server::frame = 0;
    buf[16] = server::frame / 256;
	buf[17] = server::frame % 256;
    buf[18] = sndlen / 256;
    buf[19] = sndlen % 256;
    memcpy(buf+20, snd, sndlen);

    unsigned short crc = crc_check2(buf+2, 18+sndlen);
	buf[21+sndlen] = crc / 256;
	buf[22 + sndlen] = crc % 256;
	// cout << crc << "----" << 22 + sndlen << endl;
	int ret = serv->m_tcpsend(clisock, buf, 22+sndlen);
	return ret;

}

int setLotteryOK(server* serv)
{
	int cmd = 0x1002;
	char buf[256];
	string set = "set ok";
	char *snd = const_cast<char*>(set.c_str());
	int sndlen = strlen(snd);
	memset(buf, 0, 256);
	buf[0] = 0xff;
	buf[1] = 0xff;
	memcpy(buf+2, "111111", 6);
	memcpy(buf+8, "000000", 6);
    buf[14] = cmd / 256;
    buf[15] = cmd % 256;
	if(server::frame >= 65535)
		server::frame = 0;
    buf[16] = server::frame / 256;
	buf[17] = server::frame % 256;
    buf[18] = sndlen / 256;
    buf[19] = sndlen % 256;
    memcpy(buf+20, snd, sndlen);

    unsigned short crc = crc_check2(buf+2, 18+sndlen);
	buf[21+sndlen] = crc / 256;
	buf[22 + sndlen] = crc % 256;
	// cout << crc << "----" << 22 + sndlen << endl;
	int ret = serv->m_tcpsend(clisock, buf, 22+sndlen);
	return ret;
}

int lotterytoclient(int* array, server* serv)
{
	int cmd = 0x1003;
	char buf[256];
	string s="";
	char outtime[15]; //20150303102830
	for(int i=0; i<4; ++i)
	{
		s += num2str(array[i])+"|";
	}
	s += string(lib_time_now(outtime, 1));
	cout << "ssss====" << s << endl;
	char *snd = const_cast<char*>(s.c_str());
	int sndlen = strlen(snd);
	memset(buf, 0, 256);
	buf[0] = 0xff;
	buf[1] = 0xff;
	memcpy(buf+2, "111111", 6);
	memcpy(buf+8, "000000", 6);
    buf[14] = cmd / 256;
    buf[15] = cmd % 256;
	if(server::frame >= 65535)
		server::frame = 0;
    buf[16] = server::frame / 256;
	buf[17] = server::frame % 256;
    buf[18] = sndlen / 256;
    buf[19] = sndlen % 256;
    memcpy(buf+20, snd, sndlen);

    unsigned short crc = crc_check2(buf+2, 18+sndlen);
	buf[21+sndlen] = crc / 256;
	buf[22 + sndlen] = crc % 256;
	// cout << crc << "----" << 22 + sndlen << endl;
	int ret = serv->m_tcpsend(clisock, buf, 22+sndlen);
	return 0;
	
}

int playend(server *serv)
{
	int cmd = 0x1004;
	char buf[256];
	string str = "end";
	char *snd = const_cast<char*>(str.c_str());
	int sndlen = strlen(snd);
	memset(buf, 0, 256);
	buf[0] = 0xff;
	buf[1] = 0xff;
	memcpy(buf+2, "111111", 6);
	memcpy(buf+8, "000000", 6);
    buf[14] = cmd / 256;
    buf[15] = cmd % 256;
	if(server::frame >= 65535)
		server::frame = 0;
    buf[16] = server::frame / 256;
	buf[17] = server::frame % 256;
    buf[18] = sndlen / 256;
    buf[19] = sndlen % 256;
    memcpy(buf+20, snd, sndlen);

    unsigned short crc = crc_check2(buf+2, 18+sndlen);
	buf[21+sndlen] = crc / 256;
	buf[22 + sndlen] = crc % 256;
	// cout << crc << "----" << 22 + sndlen << endl;
	int ret = serv->m_tcpsend(clisock, buf, 22+sndlen);
	return 0;

}
