/**
 * @file   protocol.c
 * @date   Wed Jun 23 09:40:39 2010
 * 
 * @brief  The code that handles the IEEE-1588 protocol and state machine
 * 
 * 
 */

#include "ptpd.h"

Boolean doInit(RunTimeOpts*,PtpClock*);
void doState(RunTimeOpts*,PtpClock*);
void toState(UInteger8,RunTimeOpts*,PtpClock*);

void handle(RunTimeOpts*,PtpClock*);
void handleAnnounce(MsgHeader*,Octet*,ssize_t,Boolean,RunTimeOpts*,PtpClock*);
void handleSync(MsgHeader*,Octet*,ssize_t,TimeInternal*,Boolean,RunTimeOpts*,PtpClock*);
void handleFollowUp(MsgHeader*,Octet*,ssize_t,Boolean,RunTimeOpts*,PtpClock*);
void handlePDelayReq(MsgHeader*,Octet*,ssize_t,TimeInternal*,Boolean,RunTimeOpts*,PtpClock*);
void handleDelayReq(MsgHeader*,Octet*,ssize_t,TimeInternal*,Boolean,RunTimeOpts*,PtpClock*);
void handlePDelayResp(MsgHeader*,Octet*,TimeInternal* ,ssize_t,Boolean,RunTimeOpts*,PtpClock*);
void handleDelayResp(MsgHeader*,Octet*,ssize_t,Boolean,RunTimeOpts*,PtpClock*);
void handlePDelayRespFollowUp(MsgHeader*,Octet*,ssize_t,Boolean,RunTimeOpts*,PtpClock*);
void handleManagement(MsgHeader*,Octet*,ssize_t,Boolean,RunTimeOpts*,PtpClock*);
void handleSignaling(MsgHeader*,Octet*,ssize_t,Boolean,RunTimeOpts*,PtpClock*);


void issueAnnounce(RunTimeOpts*,PtpClock*);
void issueSync(RunTimeOpts*,PtpClock*);
void issueFollowup(TimeInternal*,RunTimeOpts*,PtpClock*);
void issuePDelayReq(RunTimeOpts*,PtpClock*);
void issueDelayReq(RunTimeOpts*,PtpClock*);
void issuePDelayResp(TimeInternal*,MsgHeader*,RunTimeOpts*,PtpClock*);
void issueDelayResp(TimeInternal*,MsgHeader*,RunTimeOpts*,PtpClock*);
void issuePDelayRespFollowUp(TimeInternal*,MsgHeader*,RunTimeOpts*,PtpClock*);
void issueManagement(MsgHeader*,MsgManagement*,RunTimeOpts*,PtpClock*);


void addForeign(Octet*,MsgHeader*,PtpClock*);


/* loop forever. doState() has a switch for the actions and events to be
   checked for 'port_state'. the actions and events may or may not change
   'port_state' by calling toState(), but once they are done we loop around
   again and perform the actions required for the new 'port_state'. */
void 
protocol(RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	DBG("event POWERUP\n");
	
	//ptpClock->portStat = PTP_INITIALIZING
	toState(PTP_INITIALIZING, rtOpts, ptpClock);
	
	DBGV("Debug Initializing...\n");

	for(;;)
	{
		if(ptpClock->portState != PTP_INITIALIZING)
			doState(rtOpts, ptpClock);
		else if(!doInit(rtOpts, ptpClock))
			return;
		
		if(ptpClock->message_activity)
			DBGV("activity\n");
		/* else */
		  /*			DBGV("no activity\n");*/
	}
}


/* perform actions required when leaving 'port_state' and entering 'state' */
void 
toState(UInteger8 state, RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	
	ptpClock->message_activity = TRUE;
	
	/* leaving state tasks */
	switch(ptpClock->portState)
	{
	case PTP_MASTER:
		timerStop(SYNC_INTERVAL_TIMER, ptpClock->itimer);  
		timerStop(ANNOUNCE_INTERVAL_TIMER, ptpClock->itimer);
		timerStop(PDELAYREQ_INTERVAL_TIMER, ptpClock->itimer); 
		break;
		
	case PTP_SLAVE:
		timerStop(ANNOUNCE_RECEIPT_TIMER, ptpClock->itimer);
		
		if (rtOpts->E2E_mode)
			timerStop(DELAYREQ_INTERVAL_TIMER, ptpClock->itimer);
		else
			timerStop(PDELAYREQ_INTERVAL_TIMER, ptpClock->itimer);
		
		initClock(rtOpts, ptpClock); 
		break;
		
	case PTP_PASSIVE:
		timerStop(PDELAYREQ_INTERVAL_TIMER, ptpClock->itimer);
		timerStop(ANNOUNCE_RECEIPT_TIMER, ptpClock->itimer);
		break;
		
	case PTP_LISTENING:
		timerStop(ANNOUNCE_RECEIPT_TIMER, ptpClock->itimer);
		break;
		
	default:
		break;
	}
	
	/* entering state tasks */

	/*
	 * No need of PRE_MASTER state because of only ordinary clock
	 * implementation.
	 */
	
	switch(state)
	{
	case PTP_INITIALIZING:
		DBG("state PTP_INITIALIZING\n");
		ptpClock->portState = PTP_INITIALIZING;
		break;
		
	case PTP_FAULTY:
		DBG("state PTP_FAULTY\n");
		ptpClock->portState = PTP_FAULTY;
		break;
		
	case PTP_DISABLED:
		DBG("state PTP_DISABLED\n");
		ptpClock->portState = PTP_DISABLED;
		break;
		
	case PTP_LISTENING:
		DBG("state PTP_LISTENING\n");
		//ptpClock->announceReceiptTimeout��ֵΪ6��AnnounceIntervalʱ���϶
		timerStart(ANNOUNCE_RECEIPT_TIMER, 
			   (ptpClock->announceReceiptTimeout) * 
			   (pow(2,ptpClock->logAnnounceInterval)), 
			   ptpClock->itimer);
		ptpClock->portState = PTP_LISTENING;
		break;

	case PTP_MASTER:
		DBG("state PTP_MASTER\n");
		
		timerStart(SYNC_INTERVAL_TIMER, 
			   pow(2,ptpClock->logSyncInterval), ptpClock->itimer);
		DBG("SYNC INTERVAL TIMER : %f \n",
		    pow(2,ptpClock->logSyncInterval));
		timerStart(ANNOUNCE_INTERVAL_TIMER, 
			   pow(2,ptpClock->logAnnounceInterval), 
			   ptpClock->itimer);
		timerStart(PDELAYREQ_INTERVAL_TIMER, 
			   pow(2,ptpClock->logMinPdelayReqInterval), 
			   ptpClock->itimer);
		ptpClock->portState = PTP_MASTER;
		break;

	case PTP_PASSIVE:
		DBG("state PTP_PASSIVE\n");
		
		timerStart(PDELAYREQ_INTERVAL_TIMER, 
			   pow(2,ptpClock->logMinPdelayReqInterval), 
			   ptpClock->itimer);
		timerStart(ANNOUNCE_RECEIPT_TIMER, 
			   (ptpClock->announceReceiptTimeout) * 
			   (pow(2,ptpClock->logAnnounceInterval)), 
			   ptpClock->itimer);
		ptpClock->portState = PTP_PASSIVE;
		break;

	case PTP_UNCALIBRATED:
		DBG("state PTP_UNCALIBRATED\n");
		ptpClock->portState = PTP_UNCALIBRATED;
		break;

	case PTP_SLAVE:
		DBG("state PTP_SLAVE\n");
		initClock(rtOpts, ptpClock);
		
		ptpClock->waitingForFollow = FALSE;
		ptpClock->pdelay_req_send_time.seconds = 0;
		ptpClock->pdelay_req_send_time.nanoseconds = 0;
		ptpClock->pdelay_req_receive_time.seconds = 0;
		ptpClock->pdelay_req_receive_time.nanoseconds = 0;
		ptpClock->pdelay_resp_send_time.seconds = 0;
		ptpClock->pdelay_resp_send_time.nanoseconds = 0;
		ptpClock->pdelay_resp_receive_time.seconds = 0;
		ptpClock->pdelay_resp_receive_time.nanoseconds = 0;
		
		
		timerStart(ANNOUNCE_RECEIPT_TIMER,
			   (ptpClock->announceReceiptTimeout) * 
			   (pow(2,ptpClock->logAnnounceInterval)), 
			   ptpClock->itimer);
		
		if (rtOpts->E2E_mode)
			timerStart(DELAYREQ_INTERVAL_TIMER, 
				   pow(2,ptpClock->logMinDelayReqInterval), 
				   ptpClock->itimer);
		else
			timerStart(PDELAYREQ_INTERVAL_TIMER, 
				   pow(2,ptpClock->logMinPdelayReqInterval), 
				   ptpClock->itimer);

		ptpClock->portState = PTP_SLAVE;
		break;

	default:
		DBG("to unrecognized state\n");
		break;
	}

	if(rtOpts->displayStats)
		displayStats(rtOpts, ptpClock);
}

// added for debugging
char *messageTypeToStr(int messageType) {
	switch (messageType)
	{
	case SYNC:
		return "PTP_SYNC_MESSAGE";
		break;
	case DELAY_REQ:
		return "PTP_DELAY_REQ_MESSAGE";
		break;
	case PDELAY_REQ:
		return "PTP_PDELAY_REQ_MESSAGE";
		break;
	case PDELAY_RESP:
		return "PTP_PDELAY_RESP_MESSAGE";
		break;
	case FOLLOW_UP:
		return "PTP_FOLLOWUP_MESSAGE";
		break;
	case DELAY_RESP:
		return "PTP_DELAY_RESP_MESSAGE";
		break;
	case PDELAY_RESP_FOLLOW_UP:
		return "PTP_PDELAY_RESP_FOLLOWUP";
		break;
	case ANNOUNCE:
		return "PTP_ANNOUNCE_MESSAGE";
		break;
	case SIGNALING:
		return "PTP_SIGNALING_MESSAGE";
		break;
	case MANAGEMENT:
		return "PTP_MANAGEMENT_MESSAGE";
		break;
	default:
		return "UNKNOWN";
		break;
	}
}

// added for debugging
char *messageTypeToShortStr(int messageType) {
	switch (messageType)
	{
	case SYNC:
		return "SYNC";
		break;
	case DELAY_REQ:
		return "DLYREQ";
		break;
	case PDELAY_REQ:
		return "PDLYREQ";
		break;
	case PDELAY_RESP:
		return "PDLYRSP";
		break;
	case FOLLOW_UP:
		return "FOLLOWUP";
		break;
	case DELAY_RESP:
		return "DLYRESP";
		break;
	case PDELAY_RESP_FOLLOW_UP:
		return "PDLYRSPF";
		break;
	case ANNOUNCE:
		return "ANNOUNCE";
		break;
	case SIGNALING:
		return "SIGNAL";
		break;
	case MANAGEMENT:
		return "MGMTMSG";
		break;
	default:
		return "UNKNOWN";
		break;
	}
}

Boolean 
doInit(RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	DBG("manufacturerIdentity: %s\n", MANUFACTURER_ID);
	
	/* initialize networking */
	netShutdown(&ptpClock->netPath);
	if(!netInit(&ptpClock->netPath, rtOpts, ptpClock)) { //9.20
		ERROR("failed to initialize network\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return FALSE;
	}
	
	/* initialize other stuff */
	initData(rtOpts, ptpClock);
	initTimer();
	initClock(rtOpts, ptpClock);
	m1(ptpClock);
	msgPackHeader(ptpClock->msgObuf, ptpClock);
	
	//PTP_INITIALIZING->PTP_LISTENING
	toState(PTP_LISTENING, rtOpts, ptpClock);
	
	return TRUE;
}

/* handle actions and events for 'port_state'
 * ����port_state������������
 * */
void 
doState(RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	UInteger8 state;
	
	ptpClock->message_activity = FALSE;
	
	switch(ptpClock->portState)
	{
	case PTP_LISTENING:
	case PTP_PASSIVE:
	case PTP_SLAVE:
		
	case PTP_MASTER:
		/* State decision Event
		 * ����ʱ��״̬
		 * */
		if(ptpClock->record_update) //���ּ�¼�����¼��������������ʱ�ӣ�
		{
			DBGV("event STATE_DECISION_EVENT\n");
			ptpClock->record_update = FALSE;
			state = bmc(ptpClock->foreign, rtOpts, ptpClock);//����BMC�㷨����ʱ��״̬
			if(state != ptpClock->portState)
				toState(state, rtOpts, ptpClock);//״̬��Ҫ�ı�
		}
		break;
		
	default:
		break;
	}
	
	switch(ptpClock->portState)
	{
	case PTP_FAULTY:
		/* imaginary troubleshooting */
		
		DBG("event FAULT_CLEARED\n");
		toState(PTP_INITIALIZING, rtOpts, ptpClock);
		return;
		
	case PTP_LISTENING:
	case PTP_PASSIVE:
	case PTP_UNCALIBRATED:
	case PTP_SLAVE:
		
		//����ʱ��״̬
		handle(rtOpts, ptpClock);
		
		//ANNOUNCE_RECEIPT���Ĺ��ں���ж��Ƿ�ʱ�Ӹĳ���ʱ�ӻ����״̬�������Figure 23 State machine for a full implementation
		if(timerExpired(ANNOUNCE_RECEIPT_TIMER, ptpClock->itimer))  
		{
			DBGV("event ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES\n");
			ptpClock->number_foreign_records = 0;
			ptpClock->foreign_record_i = 0;
			//�ж��Ƿ�Ϊ��ʱ����Ҫ�ǿ�ptpClock->clockQuality.clockClass��ֵ
			if(!ptpClock->slaveOnly && 
			   ptpClock->clockQuality.clockClass != 255) {
				m1(ptpClock);//��ptpClock����Ϊ��ʱ��
				toState(PTP_MASTER, rtOpts, ptpClock);//ת��Ϊ��ʱ��״̬
			} else if(ptpClock->portState != PTP_LISTENING)
				toState(PTP_LISTENING, rtOpts, ptpClock);//ת��Ϊ����״̬
		}
		
		//E2Eģʽ��P2Pģʽ���͵İ���ͬ��DelayReq��PDelayReq�����ɴ�ʱ�ӷ�����
		if (rtOpts->E2E_mode) {
			if(timerExpired(DELAYREQ_INTERVAL_TIMER,
					ptpClock->itimer)) {
				DBGV("event DELAYREQ_INTERVAL_TIMEOUT_EXPIRES\n");
				issueDelayReq(rtOpts,ptpClock);
			}
		} else {
			if(timerExpired(PDELAYREQ_INTERVAL_TIMER,
					ptpClock->itimer)) {
				DBGV("event PDELAYREQ_INTERVAL_TIMEOUT_EXPIRES\n");
				issuePDelayReq(rtOpts,ptpClock);
			}
		}
		break;

	case PTP_MASTER:
		//sync������
		if(timerExpired(SYNC_INTERVAL_TIMER, ptpClock->itimer)) {
			DBGV("event SYNC_INTERVAL_TIMEOUT_EXPIRES\n");
			issueSync(rtOpts, ptpClock);
		}
		
		//Announce������
		if(timerExpired(ANNOUNCE_INTERVAL_TIMER, ptpClock->itimer)) {
			DBGV("event ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES\n");
			issueAnnounce(rtOpts, ptpClock);
		}
		
		//Ϊʲô��issuePDelayReq������issuePDelayResp
		if (!rtOpts->E2E_mode) {
			if(timerExpired(PDELAYREQ_INTERVAL_TIMER,
					ptpClock->itimer)) {
				DBGV("event PDELAYREQ_INTERVAL_TIMEOUT_EXPIRES\n");
				issuePDelayReq(rtOpts,ptpClock);
			}
		}
		
		handle(rtOpts, ptpClock);
		
		if(ptpClock->slaveOnly || 
		   ptpClock->clockQuality.clockClass == 255)
			toState(PTP_LISTENING, rtOpts, ptpClock);
		
		break;

	case PTP_DISABLED:
		handle(rtOpts, ptpClock);
		break;
		
	default:
		DBG("(doState) do unrecognized state\n");
		break;
	}
}

 
/* check and handle received messages
 * ��鲢������յı���
 * */
void 
handle(RunTimeOpts *rtOpts, PtpClock *ptpClock)
{

	int ret;
	ssize_t length;
	Boolean isFromSelf;
	TimeInternal time = { 0, 0 };
  
	//doState����ptpClock->message_activityΪFalse,
	if(!ptpClock->message_activity)	{
		ret = netSelect(0, &ptpClock->netPath);
		if(ret < 0) {
			PERROR("failed to poll sockets");
			toState(PTP_FAULTY, rtOpts, ptpClock);
			return;
		} else if(!ret) {
		  /*			DBGV("handle: nothing\n");*/
			return;
		}
		/* else length > 0 */
	}
  
	DBGV("handle: something\n");

	//�������л�ȡ������ת����buf���洢
	if(!rtOpts->ethernet_mode)
	{
		length = netRecvEvent(ptpClock->pIbuf, &time, &ptpClock->netPath); //CHANGE msgIbuf to pIbuf
		time.seconds += ptpClock->currentUtcOffset;
		if(length < 0) {
			PERROR("failed to receive on the event socket");
			toState(PTP_FAULTY, rtOpts, ptpClock);
			return;
		} else if(!length) {
			length = netRecvGeneral(ptpClock->pIbuf, &time,
						&ptpClock->netPath);
			if(length < 0) {
				PERROR("failed to receive on the general socket");
				toState(PTP_FAULTY, rtOpts, ptpClock);
				return;
			} else if(!length)
				return;
		}
	}
	else
	{
		length = macRecvEvent(ptpClock->pIbuf, &time, &ptpClock->netPath);
		time.seconds += ptpClock->currentUtcOffset;
		if(length == 1){ //not 1588 packet
			return;
		}
		else if(length < 0){
			PERROR("failed to receive on the event socket");
			toState(PTP_FAULTY, rtOpts, ptpClock);
			return;
		}
		else if(!length) {
			length = macRecvGeneral(ptpClock->pIbuf, &time, &ptpClock->netPath);
			if(length == 1){ //not 1588 packet
				return;
			}
			else if(length < 0){
				PERROR("failed to receive on the general socket");
				toState(PTP_FAULTY, rtOpts, ptpClock);
				return;
			}
			else if(!length)
					return;
		}

	}

	ptpClock->message_activity = TRUE;

	if(length < HEADER_LENGTH) {
		ERROR("message shorter than header length\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}
  
	msgUnpackHeader(ptpClock->pIbuf, &ptpClock->msgTmpHeader);//ͷ����Ϣ���

	//���汾
	if(ptpClock->msgTmpHeader.versionPTP != ptpClock->versionNumber) {
		DBGV("ignore version %d message\n", 
		     ptpClock->msgTmpHeader.versionPTP);
		return;
	}

	//���domainNumber
	if(ptpClock->msgTmpHeader.domainNumber != ptpClock->domainNumber) {
		DBGV("ignore message from domainNumber %d\n", 
		     ptpClock->msgTmpHeader.domainNumber);
		return;
	}

	//�ж�msg�Ƿ���Դ�Լ�
	/*Spec 9.5.2.2*/	
	isFromSelf = (ptpClock->portIdentity.portNumber == ptpClock->msgTmpHeader.sourcePortIdentity.portNumber
		      && !memcmp(ptpClock->msgTmpHeader.sourcePortIdentity.clockIdentity, ptpClock->portIdentity.clockIdentity, CLOCK_IDENTITY_LENGTH));

	/* 
	 * subtract the inbound latency adjustment if it is not a loop
	 *  back and the time stamp seems reasonable 
	 *  Уʱ��ƫ����
	 */
	if(!isFromSelf && time.seconds > 0)
		subTime(&time, &time, &rtOpts->inboundLatency);

	//��&time�����Ķ��Ǻ�ʱ���йصı���
	switch(ptpClock->msgTmpHeader.messageType)
	{
	case ANNOUNCE:
		DBGV("received ANNOUNCE message, entering handleAnnounce()\n");
		handleAnnounce(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
			       length, isFromSelf, rtOpts, ptpClock);
		break;
	case SYNC:
		DBGV("received SYNC message, entering handleSync()\n");
		handleSync(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
			   length, &time, isFromSelf, rtOpts, ptpClock);
		break;
	case FOLLOW_UP:
		DBGV("received FOLLOW_UP message, entering handleFollowUp()\n");
		handleFollowUp(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
			       length, isFromSelf, rtOpts, ptpClock);
		break;
	case DELAY_REQ:
		DBGV("received DELAY_REQ message, entering handleDelayReq()\n");
		handleDelayReq(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
			       length, &time, isFromSelf, rtOpts, ptpClock);
		break;
	case PDELAY_REQ:
		DBGV("received PDELAY_REQ message, entering handlePDelayReq()\n");
		handlePDelayReq(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
				length, &time, isFromSelf, rtOpts, ptpClock);
		break;  
	case DELAY_RESP:
		DBGV("received DELAY_RESP message, entering handleDelayResp()\n");
		handleDelayResp(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
				length, isFromSelf, rtOpts, ptpClock);
		break;
	case PDELAY_RESP:
		DBGV("received PDELAY_RESP message, entering handlePDelayResp()\n");
		handlePDelayResp(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
				 &time, length, isFromSelf, rtOpts, ptpClock);
		break;
	case PDELAY_RESP_FOLLOW_UP:
		DBGV("received PDELAY_RESP_FOLLOW_UP message, entering handlePDelayRespFollowUp()\n");
		handlePDelayRespFollowUp(&ptpClock->msgTmpHeader, 
					 ptpClock->pIbuf, length,
					 isFromSelf, rtOpts, ptpClock);
		break;
	case MANAGEMENT:
		DBGV("received MANAGEMENT message, entering handleManagement()\n");
		handleManagement(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
				 length, isFromSelf, rtOpts, ptpClock);
		break;
	case SIGNALING:
		DBGV("received SIGNALING message, entering handleSignaling()\n");
		handleSignaling(&ptpClock->msgTmpHeader, ptpClock->pIbuf,
				length, isFromSelf, rtOpts, ptpClock);
		break;
	default:
		DBG("handle: unrecognized message\n");
		break;
	}

	if (rtOpts->displayPackets)
		msgDump(ptpClock);
}

/* spec 9.5.3
 * Figure 29 Receipt of Announce message logic
 * */
void 
handleAnnounce(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
	       Boolean isFromSelf, RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	Boolean isFromCurrentParent = FALSE; 
 	
	DBGV("HandleAnnounce : Announce message received : \n");
	
	if(length < ANNOUNCE_LENGTH) {
		ERROR("short Announce message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}

	switch(ptpClock->portState) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
		
		DBGV("Handleannounce : disregard \n");
		return;
		
	case PTP_UNCALIBRATED:	
	case PTP_SLAVE:

		if (isFromSelf) {
			DBGV("HandleAnnounce : Ignore message from self \n");
			return;
		}
		
		/*  
		 * Valid announce message is received : BMC algorithm
		 * will be executed 
		 */
		ptpClock->record_update = TRUE; //record_updateΪTrueִ��BMC�㷨

		
		isFromCurrentParent = !memcmp(
			ptpClock->parentPortIdentity.clockIdentity,
			header->sourcePortIdentity.clockIdentity,
			CLOCK_IDENTITY_LENGTH)	&& 
			(ptpClock->parentPortIdentity.portNumber == 
			 header->sourcePortIdentity.portNumber);
	
		switch (isFromCurrentParent) {	
		case TRUE://message from current master clock, update data sets
	   		msgUnpackAnnounce(ptpClock->pIbuf,
					  &ptpClock->msgTmp.announce);
	   		s1(header,&ptpClock->msgTmp.announce,ptpClock);
	   		
	   		/*Reset Timer handling Announce receipt timeout*/
	   		timerStart(ANNOUNCE_RECEIPT_TIMER,
				   (ptpClock->announceReceiptTimeout) * 
				   (pow(2,ptpClock->logAnnounceInterval)), 
				   ptpClock->itimer);
	   		break;
	   		
		case FALSE:
	   		/*addForeign takes care of AnnounceUnpacking*/
	   		addForeign(ptpClock->pIbuf,header,ptpClock);//addForeign���ж��Ƿ�Foreign Master is already in ForeignMaster data set
	   		
	   		/*Reset Timer handling Announce receipt timeout*/
	   		timerStart(ANNOUNCE_RECEIPT_TIMER,
				   (ptpClock->announceReceiptTimeout) * 
				   (pow(2,ptpClock->logAnnounceInterval)), 
				   ptpClock->itimer);
	   		break;
	   		
		default:
	   		DBGV("HandleAnnounce : (isFromCurrentParent)"
			     "strange value ! \n");
	   		return;
	   		
		} /* switch on (isFromCurrentParrent) */
		break;
	   
	case PTP_MASTER:
	default :
	
		if (isFromSelf)	{
			DBGV("HandleAnnounce : Ignore message from self \n");
			return;
		}
		
		DBGV("Announce message from another foreign master");
		addForeign(ptpClock->pIbuf,header,ptpClock);
		ptpClock->record_update = TRUE;
		break;
	   
	} /* switch on (port_state) */

}
	
/**
 * Figure 30 Receipt of Sync message logic
 * */
void 
handleSync(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
	   TimeInternal *time, Boolean isFromSelf, 
	   RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	TimeInternal OriginTimestamp;
	TimeInternal correctionField;

	Boolean isFromCurrentParent = FALSE;
	DBGV("Sync message received : \n");
	
	if(length < SYNC_LENGTH) {
		ERROR("short Sync message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}	

	switch(ptpClock->portState) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
		
		DBGV("HandleSync : disregard \n");
		return;
		
	case PTP_UNCALIBRATED:	
	case PTP_SLAVE:
		if (isFromSelf) {
			DBGV("HandleSync: Ignore message from self \n");
			return;
		}
		isFromCurrentParent = 
			!memcmp(ptpClock->parentPortIdentity.clockIdentity,
				header->sourcePortIdentity.clockIdentity,
				CLOCK_IDENTITY_LENGTH) && 
			(ptpClock->parentPortIdentity.portNumber == 
			 header->sourcePortIdentity.portNumber);
		
		if (isFromCurrentParent) {
			/* CHANGE only upper 24bit of seconds of T2 come from os
			 * ��¼sync�հ�ʱ��
			 * Ϊ��֮ǰT2��ǰ24bit����ϵͳ��
			 * */
/*			ptpClock->sync_receive_time.seconds = time->seconds;
			ptpClock->sync_receive_time.nanoseconds = 
				time->nanoseconds;
*/
			ptpClock->os_sync_receive_time.seconds = time->seconds;
			ptpClock->os_sync_receive_time.nanoseconds = time->nanoseconds;
			ptpClock->sync_receive_time.seconds = time->seconds & 0xFFFFFF00;
			ptpClock->sync_receive_time.nanoseconds = 0x0;

			/*CHANGE ENDS*/
			if (rtOpts->recordFP) 
				fprintf(rtOpts->recordFP, "%d %llu\n", 
					header->sequenceId, 
					((time->seconds * 1000000000ULL) + 
					 time->nanoseconds));

			if ((header->flagField[0] & 0x02) == TWO_STEP_FLAG) {//˫��ģʽ
				ptpClock->waitingForFollow = TRUE;
				ptpClock->recvSyncSequenceId = 
					header->sequenceId;
				/*Save correctionField of Sync message*/
				integer64_to_internalTime(
					header->correctionfield,
					&correctionField);
				ptpClock->lastSyncCorrectionField.seconds = 
					correctionField.seconds;
				ptpClock->lastSyncCorrectionField.nanoseconds =
					correctionField.nanoseconds;
				break;
			} else {//����ģʽ
				/*the new msgUnpackSync() handle sync_receive_time too*/
				msgUnpackSync(ptpClock->pIbuf,
					      &ptpClock->msgTmp.sync, &ptpClock->sync_receive_time);
				rectifyInsertedTimestamp(&ptpClock->os_sync_receive_time,
						&ptpClock->sync_receive_time);//
				integer64_to_internalTime(
					ptpClock->msgTmpHeader.correctionfield,
					&correctionField);
				timeInternal_display(&correctionField);
				ptpClock->waitingForFollow = FALSE;
				toInternalTime(&OriginTimestamp,
					       &ptpClock->msgTmp.sync.originTimestamp);
				updateOffset(&OriginTimestamp,
					     &ptpClock->sync_receive_time,
					     &ptpClock->ofm_filt,rtOpts,
					     ptpClock,&correctionField);
				updateClock(rtOpts,ptpClock);//ͬ������ʱ�ӣ�˫��ģʽ�Ļ��ڽ��յ�FollowUp����֮����
				/*CHANGE save last sync_receive_time*/
				ptpClock->last_sync_receive_time = ptpClock->sync_receive_time;
				/*CHANGE ENDS*/
				break;
			}
		}
		break;
			
	case PTP_MASTER:
	default :
		if (!isFromSelf) {
			DBGV("HandleSync: Sync message received from "
			     "another Master  \n");
			break;
		} else {
			/*CHANGE read time from driver*/
			/*readCacheTime(time);*/
			*time = ptpClock->sync_send_driver_time;//����ģʽ��¼sync������ʱ��
			/*Add latency dont need here*/
//			addTime(time,time,&rtOpts->outboundLatency);
			issueFollowup(time,rtOpts,ptpClock);//����ģʽ�Ļ�����Ҫ�ٷ���followup���ģ���Ҫ����slave�˾����Ƿ��followup���Ľ��д���
			break;
		}	
	}
}


void 
handleFollowUp(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
	       Boolean isFromSelf, RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	DBGV("Handlefollowup : Follow up message received \n");
	
	TimeInternal preciseOriginTimestamp;
	TimeInternal correctionField;
	Boolean isFromCurrentParent = FALSE;
	
	if(length < FOLLOW_UP_LENGTH)
	{
		ERROR("short Follow up message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}
	 
	if (isFromSelf)
	{
		DBGV("Handlefollowup : Ignore message from self \n");
		return;
	}
	 
	switch(ptpClock->portState )
	{
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
	case PTP_LISTENING:
		
		DBGV("Handfollowup : disregard \n");
		return;
		
	case PTP_UNCALIBRATED:	
	case PTP_SLAVE:

		isFromCurrentParent = 
			!memcmp(ptpClock->parentPortIdentity.clockIdentity,
				header->sourcePortIdentity.clockIdentity,
				CLOCK_IDENTITY_LENGTH) && 
			(ptpClock->parentPortIdentity.portNumber == 
			 header->sourcePortIdentity.portNumber);
	 	
		if (isFromCurrentParent) {
			if (ptpClock->waitingForFollow)	{//����ǵ���ģʽ��waitingForFollowΪfalse
				if ((ptpClock->recvSyncSequenceId == 
				     header->sequenceId)) {
					msgUnpackFollowUp(ptpClock->pIbuf,
							  &ptpClock->msgTmp.follow);
					ptpClock->waitingForFollow = FALSE;
					toInternalTime(&preciseOriginTimestamp,
						       &ptpClock->msgTmp.follow.preciseOriginTimestamp);
					integer64_to_internalTime(ptpClock->msgTmpHeader.correctionfield,
								  &correctionField);
					addTime(&correctionField,&correctionField,
						&ptpClock->lastSyncCorrectionField);
					updateOffset(&preciseOriginTimestamp,
						     &ptpClock->sync_receive_time,&ptpClock->ofm_filt,
						     rtOpts,ptpClock,
						     &correctionField);
					updateClock(rtOpts,ptpClock);
					break;	 		
				} else 
					DBGV("SequenceID doesn't match with "
					     "last Sync message \n");
			} else 
				DBGV("Slave was not waiting a follow up "
				     "message \n");
		} else 
			DBGV("Follow up message is not from current parent \n");

	case PTP_MASTER:
		DBGV("Follow up message received from another master \n");
		break;
			
	default:
    		DBG("do unrecognized state\n");
    		break;
	} /* Switch on (port_state) */

}


void 
handleDelayReq(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
	       TimeInternal *time, Boolean isFromSelf,
	       RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	if (! rtOpts->E2E_mode) {
		/* (Peer to Peer mode) */
		ERROR("Delay messages are disregarded in Peer to Peer mode \n");
		return;
	}

	DBGV("delayReq message received : \n");
	
	if(length < DELAY_REQ_LENGTH) {
		ERROR("short DelayReq message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}

	switch(ptpClock->portState) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
	case PTP_UNCALIBRATED:
	case PTP_LISTENING:
		DBGV("HandledelayReq : disregard \n");
		return;

	case PTP_SLAVE:
		if (isFromSelf)	{
			/* 
			 * Get sending timestamp from IP stack
			 * with So_TIMESTAMP
			 */
			/*
			ptpClock->delay_req_send_time.seconds = 
				time->seconds;
			ptpClock->delay_req_send_time.nanoseconds = 
				time->nanoseconds;
			*/
			ptpClock->delay_req_send_time.seconds =
				time->seconds & 0xFFFFFF00;
			ptpClock->delay_req_send_time.nanoseconds =
					0;
			ptpClock->os_delay_req_send_time.seconds =
					time->seconds;
			ptpClock->os_delay_req_send_time.nanoseconds =
					time->nanoseconds;
			/*Add latency*/

			addTime(&ptpClock->delay_req_send_time,
				&ptpClock->delay_req_send_time,
				&rtOpts->outboundLatency);
			break;
		}
		break;

	case PTP_MASTER:
		msgUnpackHeader(ptpClock->pIbuf,
				&ptpClock->delayReqHeader);
		//CHANGE ADD NEW DISPLAY FUNCTION
#ifdef PTPD_DBG
		msgDisplayBuf(ptpClock->pIbuf,44);
#endif /* PTPD_DBG */
		msgUnpackDelayReq(ptpClock->pIbuf,
			      &ptpClock->msgTmp.req);
		TimeInternal OriginTimeStamp;
		toInternalTime(&OriginTimeStamp, &ptpClock->msgTmp.req.originTimestamp);
		issueDelayResp(&OriginTimeStamp,&ptpClock->delayReqHeader,
			       rtOpts,ptpClock);
		break;

	default:
		DBG("do unrecognized state\n");
		break;
	}
}

void 
handleDelayResp(MsgHeader *header, Octet *msgIbuf, ssize_t length,
		Boolean isFromSelf, RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	if (! rtOpts->E2E_mode) {
		/* (Peer to Peer mode) */
		ERROR("Delay messages are disregarded in Peer to Peer mode\n");
		return;
	}

	Boolean isFromCurrentParent = FALSE;
	TimeInternal requestReceiptTimestamp;
	TimeInternal correctionField;

	DBGV("delayResp message received : \n");

	if(length < DELAY_RESP_LENGTH) {
		ERROR("short DelayResp message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}

	switch(ptpClock->portState) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
	case PTP_UNCALIBRATED:
	case PTP_LISTENING:
		DBGV("HandledelayResp : disregard \n");
		return;

	case PTP_SLAVE:
		msgUnpackDelayResp(ptpClock->pIbuf,
				   &ptpClock->msgTmp.resp, &ptpClock->delay_req_send_time);
		/*CHANGE add rectification here to solve the carry problem*/
		rectifyInsertedTimestamp(&ptpClock->os_delay_req_send_time,
				&ptpClock->delay_req_send_time);
		if ((memcmp(ptpClock->parentPortIdentity.clockIdentity,
			    header->sourcePortIdentity.clockIdentity,
			    CLOCK_IDENTITY_LENGTH) == 0 ) &&
		    (ptpClock->parentPortIdentity.portNumber == 
		     header->sourcePortIdentity.portNumber))
			isFromCurrentParent = TRUE;
		
		if ((memcmp(ptpClock->portIdentity.clockIdentity,
			    ptpClock->msgTmp.resp.requestingPortIdentity.clockIdentity,
			    CLOCK_IDENTITY_LENGTH) == 0) &&
		    ((ptpClock->sentDelayReqSequenceId - 1)== 
		     header->sequenceId) &&
		    (ptpClock->portIdentity.portNumber == 
		     ptpClock->msgTmp.resp.requestingPortIdentity.portNumber)
		    && isFromCurrentParent) {
			toInternalTime(&requestReceiptTimestamp,
				       &ptpClock->msgTmp.resp.receiveTimestamp);
			ptpClock->delay_req_receive_time.seconds = 
				requestReceiptTimestamp.seconds;
			ptpClock->delay_req_receive_time.nanoseconds = 
				requestReceiptTimestamp.nanoseconds;

			integer64_to_internalTime(
				header->correctionfield,
				&correctionField);
			updateDelay(&ptpClock->owd_filt,
				    rtOpts,ptpClock, &correctionField);

			ptpClock->logMinDelayReqInterval = 
				header->logMessageInterval;
		} else {
			DBGV("HandledelayResp : delayResp doesn't match with the delayReq. \n");
			break;
		}
	}
}


void 
handlePDelayReq(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
		TimeInternal *time, Boolean isFromSelf, 
		RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	if (rtOpts->E2E_mode) {
		/* (End to End mode..) */
		ERROR("Peer Delay messages are disregarded in End to End mode \n");
		return;
	}

	DBGV("PdelayReq message received : \n");

	if(length < PDELAY_REQ_LENGTH) {
		ERROR("short PDelayReq message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}	

	switch(ptpClock->portState ) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
	case PTP_UNCALIBRATED:
	case PTP_LISTENING:
		DBGV("HandlePdelayReq : disregard \n");
		return;
	
	case PTP_SLAVE:
	case PTP_MASTER:
	case PTP_PASSIVE:
	
		if (isFromSelf) {
			/* 
			 * Get sending timestamp from IP stack
			 * with So_TIMESTAMP
			 */
			DBGV("is from self!! \n");
			ptpClock->pdelay_req_send_time.seconds = 
				time->seconds;
			ptpClock->pdelay_req_send_time.nanoseconds = 
				time->nanoseconds;
		
			/*Add latency*/
			addTime(&ptpClock->pdelay_req_send_time,
				&ptpClock->pdelay_req_send_time,
				&rtOpts->outboundLatency);
			break;
		} else {
			msgUnpackHeader(ptpClock->pIbuf,
					&ptpClock->PdelayReqHeader);
			/*CHANGE get t2 from p-delay-req's time stamp field and send them in p-delay-resp*/
#ifdef PTPD_DBG
			msgDisplayBuf(ptpClock->pIbuf,54);
#endif /* PTPD_DBG */
			msgUnpackPDelayReq(ptpClock->pIbuf, &ptpClock->msgTmp.preq);
			TimeInternal originTimestamp;
			toInternalTime(&originTimestamp,
				       &ptpClock->msgTmp.preq.originTimestamp);
			issuePDelayResp(&originTimestamp, header, rtOpts, ptpClock);
			break;
		}
	default:
		DBG("do unrecognized state\n");
		break;
	}
}

void 
handlePDelayResp(MsgHeader *header, Octet *msgIbuf, TimeInternal *time,
		 ssize_t length, Boolean isFromSelf, 
		 RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	if (rtOpts->E2E_mode) {
		/* (End to End mode..) */
		ERROR("Peer Delay messages are disregarded in End to End mode \n");
		return;
	}

	Boolean isFromCurrentParent = FALSE;
	TimeInternal requestReceiptTimestamp;
	TimeInternal correctionField;

	DBGV("PdelayResp message received : \n");

	if(length < PDELAY_RESP_LENGTH)	{
		ERROR("short PDelayResp message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}	

	switch(ptpClock->portState ) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
	case PTP_UNCALIBRATED:
	case PTP_LISTENING:
		DBGV("HandlePdelayResp : disregard \n");
		return;
	
	case PTP_SLAVE:
		if (isFromSelf)	{

			addTime(time,time,&rtOpts->outboundLatency);
			issuePDelayRespFollowUp(time,
						&ptpClock->PdelayReqHeader,
						rtOpts,ptpClock);
			break;
		}
		msgUnpackPDelayResp(ptpClock->pIbuf,
				    &ptpClock->msgTmp.presp);
	
		isFromCurrentParent = !memcmp(ptpClock->parentPortIdentity.clockIdentity,
					      header->sourcePortIdentity.clockIdentity,CLOCK_IDENTITY_LENGTH) && 
			(ptpClock->parentPortIdentity.portNumber == 
			 header->sourcePortIdentity.portNumber);

		if (!((ptpClock->sentPDelayReqSequenceId == 
		       header->sequenceId) && 
		      (!memcmp(ptpClock->portIdentity.clockIdentity,ptpClock->msgTmp.presp.requestingPortIdentity.clockIdentity,CLOCK_IDENTITY_LENGTH))
			 && ( ptpClock->portIdentity.portNumber == ptpClock->msgTmp.presp.requestingPortIdentity.portNumber)))	{

			/* Two Step Clock */
			if ((header->flagField[0] & 0x02) == 
			    TWO_STEP_FLAG) {
				/*Store t4 (Fig 35)*/
				ptpClock->pdelay_resp_receive_time.seconds = time->seconds;
				ptpClock->pdelay_resp_receive_time.nanoseconds = time->nanoseconds;
				/*store t2 (Fig 35)*/
				toInternalTime(&requestReceiptTimestamp,
					       &ptpClock->msgTmp.presp.requestReceiptTimestamp);
				ptpClock->pdelay_req_receive_time.seconds = requestReceiptTimestamp.seconds;
				ptpClock->pdelay_req_receive_time.nanoseconds = requestReceiptTimestamp.nanoseconds;
				
				integer64_to_internalTime(header->correctionfield,&correctionField);
				ptpClock->lastPdelayRespCorrectionField.seconds = correctionField.seconds;
				ptpClock->lastPdelayRespCorrectionField.nanoseconds = correctionField.nanoseconds;
				break;
			} else {
			/* One step Clock */
				/*Store t4 (Fig 35)*/
				ptpClock->pdelay_resp_receive_time.seconds = time->seconds;
				ptpClock->pdelay_resp_receive_time.nanoseconds = time->nanoseconds;
				
				integer64_to_internalTime(header->correctionfield,&correctionField);
				updatePeerDelay (&ptpClock->owd_filt,rtOpts,ptpClock,&correctionField,FALSE);

				break;
			}
		} else {
			DBGV("HandlePdelayResp : Pdelayresp doesn't "
			     "match with the PdelayReq. \n");
			break;
		}
		break; /* XXX added by gnn for safety */
	case PTP_MASTER:
		/*Loopback Timestamp*/
		if (isFromSelf) {

			*time = ptpClock->pdelay_resp_send_driver_time;
			/*Add latency CHANGE dont need here*/
//			addTime(time,time,&rtOpts->outboundLatency);
				
			issuePDelayRespFollowUp(
				time,
				&ptpClock->PdelayReqHeader,
				rtOpts, ptpClock);
			break;
		}
		msgUnpackPDelayResp(ptpClock->pIbuf,
				    &ptpClock->msgTmp.presp);
	
		isFromCurrentParent = !memcmp(ptpClock->parentPortIdentity.clockIdentity,header->sourcePortIdentity.clockIdentity,CLOCK_IDENTITY_LENGTH)
			&& (ptpClock->parentPortIdentity.portNumber == header->sourcePortIdentity.portNumber);

		if (!((ptpClock->sentPDelayReqSequenceId == 
		       header->sequenceId) && 
		      (!memcmp(ptpClock->portIdentity.clockIdentity,
			       ptpClock->msgTmp.presp.requestingPortIdentity.clockIdentity,
			       CLOCK_IDENTITY_LENGTH)) && 
		      (ptpClock->portIdentity.portNumber == 
		       ptpClock->msgTmp.presp.requestingPortIdentity.portNumber))) {
			/* Two Step Clock */
			if ((header->flagField[0] & 0x02) == 
			    TWO_STEP_FLAG) {
				/*Store t4 (Fig 35)*/
				ptpClock->pdelay_resp_receive_time.seconds = time->seconds;
				ptpClock->pdelay_resp_receive_time.nanoseconds = time->nanoseconds;
				/*store t2 (Fig 35)*/
				toInternalTime(&requestReceiptTimestamp,
					       &ptpClock->msgTmp.presp.requestReceiptTimestamp);
				ptpClock->pdelay_req_receive_time.seconds = 
					requestReceiptTimestamp.seconds;
				ptpClock->pdelay_req_receive_time.nanoseconds = 
					requestReceiptTimestamp.nanoseconds;
				integer64_to_internalTime(
					header->correctionfield,
					&correctionField);
				ptpClock->lastPdelayRespCorrectionField.seconds = correctionField.seconds;
				ptpClock->lastPdelayRespCorrectionField.nanoseconds = correctionField.nanoseconds;
				break;
			} else { /* One step Clock */
				/*Store t4 (Fig 35)*/
				ptpClock->pdelay_resp_receive_time.seconds = time->seconds;
				ptpClock->pdelay_resp_receive_time.nanoseconds = time->nanoseconds;
				
				integer64_to_internalTime(
					header->correctionfield,
					&correctionField);
				updatePeerDelay(&ptpClock->owd_filt,
						rtOpts,ptpClock,
						&correctionField,FALSE);
				break;
			}
		}
		break; /* XXX added by gnn for safety */
	default:
		DBG("do unrecognized state\n");
		break;
	}
}

void 
handlePDelayRespFollowUp(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
			 Boolean isFromSelf, RunTimeOpts *rtOpts, 
			 PtpClock *ptpClock){

	if (rtOpts->E2E_mode) {
		/* (End to End mode..) */
		ERROR("Peer Delay messages are disregarded in End to End mode \n");
		return;
	}

	TimeInternal responseOriginTimestamp;
	TimeInternal correctionField;

	DBGV("PdelayRespfollowup message received : \n");

	if(length < PDELAY_RESP_FOLLOW_UP_LENGTH) {
		ERROR("short PDelayRespfollowup message\n");
		toState(PTP_FAULTY, rtOpts, ptpClock);
		return;
	}	

	switch(ptpClock->portState) {
	case PTP_INITIALIZING:
	case PTP_FAULTY:
	case PTP_DISABLED:
	case PTP_UNCALIBRATED:
		DBGV("HandlePdelayResp : disregard \n");
		return;
	
	case PTP_SLAVE:
		if (header->sequenceId == 
		    ptpClock->sentPDelayReqSequenceId-1) {
			msgUnpackPDelayRespFollowUp(
				ptpClock->pIbuf,
				&ptpClock->msgTmp.prespfollow);
			toInternalTime(
				&responseOriginTimestamp,
				&ptpClock->msgTmp.prespfollow.responseOriginTimestamp);
			ptpClock->pdelay_resp_send_time.seconds = 
				responseOriginTimestamp.seconds;
			ptpClock->pdelay_resp_send_time.nanoseconds = 
				responseOriginTimestamp.nanoseconds;
			integer64_to_internalTime(
				ptpClock->msgTmpHeader.correctionfield,
				&correctionField);
			addTime(&correctionField,&correctionField,
				&ptpClock->lastPdelayRespCorrectionField);
			updatePeerDelay (&ptpClock->owd_filt,
					 rtOpts, ptpClock,
					 &correctionField,TRUE);
			break;
		}
	case PTP_MASTER:
		if (header->sequenceId == 
		    ptpClock->sentPDelayReqSequenceId-1) {
			msgUnpackPDelayRespFollowUp(
				ptpClock->pIbuf,
				&ptpClock->msgTmp.prespfollow);
			toInternalTime(&responseOriginTimestamp,
				       &ptpClock->msgTmp.prespfollow.responseOriginTimestamp);
			ptpClock->pdelay_resp_send_time.seconds = 
				responseOriginTimestamp.seconds;
			ptpClock->pdelay_resp_send_time.nanoseconds = 
				responseOriginTimestamp.nanoseconds;
			integer64_to_internalTime(
				ptpClock->msgTmpHeader.correctionfield,
				&correctionField);
			addTime(&correctionField, 
				&correctionField,
				&ptpClock->lastPdelayRespCorrectionField);
			updatePeerDelay(&ptpClock->owd_filt,
					rtOpts, ptpClock,
					&correctionField,TRUE);
			break;
		}
	default:
		DBGV("Disregard PdelayRespFollowUp message  \n");
	}
}

void 
handleManagement(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
		 Boolean isFromSelf, RunTimeOpts *rtOpts, PtpClock *ptpClock)
{}

void 
handleSignaling(MsgHeader *header, Octet *msgIbuf, ssize_t length, 
		     Boolean isFromSelf, RunTimeOpts *rtOpts, 
		     PtpClock *ptpClock)
{}


/*Pack and send on general multicast ip adress an Announce message*/
void 
issueAnnounce(RunTimeOpts *rtOpts,PtpClock *ptpClock)
{
	msgPackAnnounce(ptpClock->msgObuf,ptpClock);
	msgPackFlag(ptpClock->msgObuf,ptpClock);

	if(!rtOpts->ethernet_mode)
	{
		if (!netSendGeneral(ptpClock->msgObuf,ANNOUNCE_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("Announce message can't be sent -> FAULTY state \n");
		} else {
			DBGV("Announce MSG sent ! \n");
			ptpClock->sentAnnounceSequenceId++;
		}
	}
	else
	{
		if (!macSendGeneral(ptpClock->msgObuf,ANNOUNCE_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("Announce message can't be sent -> FAULTY state \n");
		} else {
			DBGV("Announce MSG sent ! \n");
			ptpClock->sentAnnounceSequenceId++;
		}
	}
}



/*Pack and send on event multicast ip adress a Sync message*/
void 
issueSync(RunTimeOpts *rtOpts,PtpClock *ptpClock)
{
	Timestamp originTimestamp;
	TimeInternal internalTime;
	getTime(&internalTime);
	fromInternalTime(&internalTime,&originTimestamp);
	
	/*CHANGE! make the last 32bit sec and entire 32 nanosec of origianTimestamp
	 *  to zero to test driver*/
	originTimestamp.secondsField.lsb &= (UInteger32)0x0;	/*keep the highest 16bit, why???*/
	originTimestamp.nanosecondsField &= (UInteger32)0x0;
	/*CHANGE ENDS*/

	//sync����Ҫ��ʱ��������sync
	msgPackSync(ptpClock->msgObuf,&originTimestamp,ptpClock);
	
	/*CHANGE! SYNC_LENGTH*/
	if(!rtOpts->ethernet_mode)
	{
		if (!netSendEvent(ptpClock->msgObuf,SYNC_LENGTH,&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("Sync message can't be sent -> FAULTY state \n");
		} else {
			DBGV("Sync MSG sent ! \n");
			ptpClock->sentSyncSequenceId++;
		}
	}
	else
	{
		if (!macSendEvent(ptpClock->msgObuf,SYNC_LENGTH,&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("Sync message can't be sent -> FAULTY state \n");
		} else {
			DBGV("Sync MSG sent ! \n");
			ptpClock->sentSyncSequenceId++;
		}
	}
	/*CHANGE got sync_send_time here
	 * �о��������ȡsync���ķ���ʱ�䲻����ȷ
	 * */
	DBGV("getting sync_send_time.. \n");
	TimeInternal sync_send_time;
	//ptpClock->fd֮ǰopenʧ�ܣ�ֵӦ��Ϊ-1��
	readCacheTime(&sync_send_time, ptpClock->fd);
	ptpClock->sync_send_driver_time = sync_send_time;
	//change add a new display function
	timeInternal_readable(&sync_send_time,"sync");

}


/*Pack and send on general multicast ip adress a FollowUp message*/
void 
issueFollowup(TimeInternal *time,RunTimeOpts *rtOpts,PtpClock *ptpClock)
{
	Timestamp preciseOriginTimestamp;
	fromInternalTime(time,&preciseOriginTimestamp);
	
	msgPackFollowUp(ptpClock->msgObuf,&preciseOriginTimestamp,ptpClock);

	if(!rtOpts->ethernet_mode)
	{
		if (!netSendGeneral(ptpClock->msgObuf,FOLLOW_UP_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("FollowUp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("FollowUp MSG sent ! \n");
		}
	}
	else
	{
		if (!macSendGeneral(ptpClock->msgObuf,FOLLOW_UP_LENGTH,
				    &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("FollowUp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("FollowUp MSG sent ! \n");
		}
	}
}


/*Pack and send on event multicast ip adress a DelayReq message*/
void 
issueDelayReq(RunTimeOpts *rtOpts,PtpClock *ptpClock)
{
	Timestamp originTimestamp;
	TimeInternal internalTime;
	getTime(&internalTime);
	fromInternalTime(&internalTime,&originTimestamp);
	/*CHANGE! make the last 32bit sec and entire 32 nanosec of origianTimestamp
	 *  to zero to test driver*/
	originTimestamp.secondsField.lsb &= (UInteger32)0x0;	/*keep the highest 16bit*/
	originTimestamp.nanosecondsField &= (UInteger32)0x0;
	/*CHANGE ENDS*/
	msgPackDelayReq(ptpClock->msgObuf,&originTimestamp,ptpClock);

	if(!rtOpts->ethernet_mode)
	{
		if (!netSendEvent(ptpClock->msgObuf,DELAY_REQ_LENGTH,
				  &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("delayReq message can't be sent -> FAULTY state \n");
		} else {
			DBGV("DelayReq MSG sent ! \n");
			ptpClock->sentDelayReqSequenceId++;
		}
	}
	else
	{
		if (!macSendEvent(ptpClock->msgObuf,DELAY_REQ_LENGTH,
				  &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("delayReq message can't be sent -> FAULTY state \n");
		} else {
			DBGV("DelayReq MSG sent ! \n");
			ptpClock->sentDelayReqSequenceId++;
		}
	}
}

/*Pack and send on event multicast ip adress a PDelayReq message*/
void 
issuePDelayReq(RunTimeOpts *rtOpts,PtpClock *ptpClock)
{
	Timestamp originTimestamp;
	TimeInternal internalTime;
	getTime(&internalTime);
	fromInternalTime(&internalTime,&originTimestamp);
	/*CHANGE! make the last 32bit sec and entire 32 nanosec of origianTimestamp
	 *  to zero to test driver*/
	originTimestamp.secondsField.lsb &= (UInteger32)0x0;	/*keep the highest 16bit*/
	originTimestamp.nanosecondsField &= (UInteger32)0x0;
	/*CHANGE ENDS*/
	msgPackPDelayReq(ptpClock->msgObuf,&originTimestamp,ptpClock);

	if(!rtOpts->ethernet_mode)
	{
		if (!netSendPeerEvent(ptpClock->msgObuf,PDELAY_REQ_LENGTH,
					  &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("PdelayReq message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayReq MSG sent ! \n");
			ptpClock->sentPDelayReqSequenceId++;
		}
	}
	else
	{
		if (!macSendPeerEvent(ptpClock->msgObuf,PDELAY_REQ_LENGTH,
					  &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("PdelayReq message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayReq MSG sent ! \n");
			ptpClock->sentPDelayReqSequenceId++;
		}
	}
	/*CHANGE read time(do nothing) to free buffer in driver*/
	readCacheTime(&internalTime,ptpClock->fd);
}

/*Pack and send on event multicast ip adress a PDelayResp message*/
void 
issuePDelayResp(TimeInternal *time,MsgHeader *header,RunTimeOpts *rtOpts,
		PtpClock *ptpClock)
{
	DBGV("issuing pDelayResp \n");
	Timestamp requestReceiptTimestamp;
	fromInternalTime(time,&requestReceiptTimestamp);
	msgPackPDelayResp(ptpClock->msgObuf,header,
			  &requestReceiptTimestamp,ptpClock);

	if(!rtOpts->ethernet_mode)
	{
		if (!netSendPeerEvent(ptpClock->msgObuf,PDELAY_RESP_LENGTH,
					  &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("PdelayResp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayResp MSG sent ! \n");
		}
	}
	else
	{
		if (!macSendPeerEvent(ptpClock->msgObuf,PDELAY_RESP_LENGTH,
					  &ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("PdelayResp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayResp MSG sent ! \n");
		}
	}
	/*CHANGE GOT pDelay_resp_send_time here*/
	TimeInternal pdelay_resp_send_time;
	readCacheTime(&pdelay_resp_send_time,ptpClock->fd);
	ptpClock->pdelay_resp_send_driver_time = pdelay_resp_send_time;
}


/*Pack and send on event multicast ip adress a DelayResp message*/
void 
issueDelayResp(TimeInternal *time,MsgHeader *header,RunTimeOpts *rtOpts,
	       PtpClock *ptpClock)
{
	Timestamp requestReceiptTimestamp;
	fromInternalTime(time,&requestReceiptTimestamp);
	msgPackDelayResp(ptpClock->msgObuf,header,&requestReceiptTimestamp,
			 ptpClock);
	if(!rtOpts->ethernet_mode)
	{
		if (!netSendGeneral(ptpClock->msgObuf,DELAY_RESP_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("delayResp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayResp MSG sent ! \n");
		}
	}
	else
	{
		if (!macSendGeneral(ptpClock->msgObuf,DELAY_RESP_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("delayResp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayResp MSG sent ! \n");
		}
	}
}



void issuePDelayRespFollowUp(TimeInternal *time, MsgHeader *header,
			     RunTimeOpts *rtOpts, PtpClock *ptpClock)
{
	Timestamp responseOriginTimestamp;
	fromInternalTime(time,&responseOriginTimestamp);

	msgPackPDelayRespFollowUp(ptpClock->msgObuf,header,
				  &responseOriginTimestamp,ptpClock);

	if(!rtOpts->ethernet_mode)
	{
		if (!netSendPeerGeneral(ptpClock->msgObuf,
					PDELAY_RESP_FOLLOW_UP_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("PdelayRespFollowUp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayRespFollowUp MSG sent ! \n");
		}
	}
	else
	{
		if (!macSendPeerGeneral(ptpClock->msgObuf,
					PDELAY_RESP_FOLLOW_UP_LENGTH,
					&ptpClock->netPath)) {
			toState(PTP_FAULTY,rtOpts,ptpClock);
			DBGV("PdelayRespFollowUp message can't be sent -> FAULTY state \n");
		} else {
			DBGV("PDelayRespFollowUp MSG sent ! \n");
		}
	}
}

void 
issueManagement(MsgHeader *header,MsgManagement *manage,RunTimeOpts *rtOpts,
		PtpClock *ptpClock)
{}

void 
addForeign(Octet *buf,MsgHeader *header,PtpClock *ptpClock)
{
	int i,j;
	Boolean found = FALSE;

	j = ptpClock->foreign_record_best;
	
	/*Check if Foreign master is already known*/
	for (i=0;i<ptpClock->number_foreign_records;i++) {
		if (!memcmp(header->sourcePortIdentity.clockIdentity,
			    ptpClock->foreign[j].foreignMasterPortIdentity.clockIdentity,
			    CLOCK_IDENTITY_LENGTH) && 
		    (header->sourcePortIdentity.portNumber == 
		     ptpClock->foreign[j].foreignMasterPortIdentity.portNumber))
		{//update existing foreignMasterDS record
			/*Foreign Master is already in Foreignmaster data set*/
			ptpClock->foreign[j].foreignMasterAnnounceMessages++; 
			found = TRUE;
			DBGV("addForeign : AnnounceMessage incremented \n");
			msgUnpackHeader(buf,&ptpClock->foreign[j].header);
			msgUnpackAnnounce(buf,&ptpClock->foreign[j].announce);
			break;
		}
	
		j = (j+1)%ptpClock->number_foreign_records;
	}

	/*New Foreign Master*/
	if (!found) {	//create new foreignMasterDS record
		if (ptpClock->number_foreign_records < 
		    ptpClock->max_foreign_records) {
			ptpClock->number_foreign_records++;
		}
		j = ptpClock->foreign_record_i;
		
		/*Copy new foreign master data set from Announce message*/
		memcpy(ptpClock->foreign[j].foreignMasterPortIdentity.clockIdentity,
		       header->sourcePortIdentity.clockIdentity,
		       CLOCK_IDENTITY_LENGTH);
		ptpClock->foreign[j].foreignMasterPortIdentity.portNumber = 
			header->sourcePortIdentity.portNumber;
		ptpClock->foreign[j].foreignMasterAnnounceMessages = 0;
		
		/*
		 * header and announce field of each Foreign Master are
		 * usefull to run Best Master Clock Algorithm
		 */
		msgUnpackHeader(buf,&ptpClock->foreign[j].header);
		msgUnpackAnnounce(buf,&ptpClock->foreign[j].announce);
		DBGV("New foreign Master added \n");
		
		ptpClock->foreign_record_i = 
			(ptpClock->foreign_record_i+1) % 
			ptpClock->max_foreign_records;	
	}
}
