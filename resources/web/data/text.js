var LangText={
    "en": {
		"t1": "Welcome to QIDISlicer",
		"t2": "User Guide",
		"t3": "First Print",
		"t4": "Support Generate",
		"t5": "Issue Report",
    },
    "zh_CN": {
		"t1": "欢迎使用QIDISlicer",
		"t2": "用户指南",
		"t3": "首次打印",
		"t4": "支撑生成",
		"t5": "错误报告",
    },
    "ja_JP": {
		"t1": "QIDISlicerへようこそ",
		"t2": "ユーザーガイド",
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
