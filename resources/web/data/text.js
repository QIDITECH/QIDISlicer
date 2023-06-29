var LangText={
    "en": {
		"t0": "Welcome to QIDISlicer",
		"t1": "User Guide",
		"t2": "First Print",
		"t3": "Add Support",
		"t4": "Connect Device",
		"t5": "Wifi Send",
		"t6": "Issue Report",
    },
    "zh_CN": {
		"t0": "欢迎使用QIDISlicer",
		"t1": "用户指南",
		"t2": "首次打印",
		"t3": "添加支撑",
		"t4": "连接设备",
		"t5": "无线发送",
		"t6": "问题报告",
    },
    "ja_JP": {
		"t0": "QIDISlicerへようこそ",
		"t1": "ユーザーガイド",
		"t2": "最初の印刷物",
		"t3": "サポートを追加する",
		"t4": "デバイスを接続する",
		"t5": "Wi-Fi送信",
		"t6": "問題レポート",
    }
};


var LANG_COOKIE_NAME="QIDIWebLang";
var LANG_COOKIE_EXPIRESECOND= 365*86400;

function TranslatePage()
{
	let strLang=GetQueryString("lang");
	if(strLang!=null)
	{
		//setCookie(LANG_COOKIE_NAME,strLang,LANG_COOKIE_EXPIRESECOND,'/');
		localStorage.setItem(LANG_COOKIE_NAME,strLang);
	}
	else
	{
		//strLang=getCookie(LANG_COOKIE_NAME);
		strLang=localStorage.getItem(LANG_COOKIE_NAME);
	}
	
	//alert(strLang);
	
	if( !LangText.hasOwnProperty(strLang) )
		strLang="en";
	
    let AllNode=$(".trans");
	let nTotal=AllNode.length;
	for(let n=0;n<nTotal;n++)
	{
		let OneNode=AllNode[n];
		
		let tid=$(OneNode).attr("tid");
		if( LangText[strLang].hasOwnProperty(tid) )
		{
			$(OneNode).html(LangText[strLang][tid]);
		}
	}
}
