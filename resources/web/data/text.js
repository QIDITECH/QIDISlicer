var LangText={
	"en": {
		"t0": "Welcome to QIDISlicer",
		"t1": "User Guide",
		"t2": "First Print",
		"t3": "Add Support",
		"t4": "Connect Device",
		"t5": "Wifi Send",
		"t6": "Issue Report",
		"t7": "Demonstration",
		"t8": "Product Info",
		"t9": "Contact with us",
		"t10": "Filament",
	},
	"zh_CN": {
		"t0": "欢迎使用QIDISlicer",
		"t1": "用户指南",
		"t2": "首次打印",
		"t3": "添加支撑",
		"t4": "连接设备",
		"t5": "无线发送",
		"t6": "问题报告",
		"t7": "演示",
		"t8": "产品信息",
		"t9": "与我们联系",
		"t10": "耗材",
	},
	"ja": {
		"t0": "QIDISlicerへようこそ",
		"t1": "ユーザーガイド",
		"t2": "最初の印刷物",
		"t3": "サポートを追加する",
		"t4": "デバイスを接続する",
		"t5": "Wi-Fi送信",
		"t6": "問題レポート",
		"t7": "デモンストレーション",
		"t8": "製品情報",
		"t9": "お問い合わせください",
		"t10": "フィラメント",
	},
	"fr": {
		"t0": "Bienvenue dans QIDISlicer",
		"t1": "Guide de l'utilisateur",
		"t2": "Première d'impression",
		"t3": "Ajouter une prise en charge",
		"t4": "Connecter l'appareil",
		"t5": "Envoi Wi-Fi",
		"t6": "Rapport de problème",
		"t7": "Démonstration",
		"t8": "Informations sur le produit",
		"t9": "Contactez-nous",
		"t10": "Filament",
	},
	"de": {
		"t0": "Willkommen bei QIDISlicer",
		"t1": "Benutzerhandbuch",
		"t2": "Erster Druck",
		"t3": "Unterstützung hinzufügen",
		"t4": "Gerät verbinden",
		"t5": "Wi-Fi senden",
		"t6": "Problembericht",
		"t7": "Demonstration",
		"t8": "Produktinformationen",
		"t9": "Kontaktieren Sie uns",
		"t10": "Filament",
	},
	"be": {
		"t0": "Вітаем у QIDISlicer",
		"t1": "Кіраўніцтва карыстальніка",
		"t2": "Першы адбітак",
		"t3": "Дадаць падтрымку",
		"t4": "Падключыць прыладу",
		"t5": "Адправіць па Wi-Fi",
		"t6": "Паведамленне аб праблеме",
		"t7": "Дэманстрацыя",
		"t8": "Інфармацыя аб прадукце",
		"t9": "Звязацца з намі",
		"t10": "Філамент",
	},
	"ca": {
		"t0": "Benvingut a QIDISlicer",
		"t1": "Guia de l'usuari",
		"t2": "Primera d'impressió",
		"t3": "Afegeix suport",
		"t4": "Connecta el dispositiu",
		"t5": "Enviament sense fil",
		"t6": "Informe de problemes",
		"t7": "Demostració",
		"t8": "Informació del producte",
		"t9": "Contacteu amb nosaltres",
		"t10": "Filament",
	},
	"cs": {
		"t0": "Vítejte v QIDISlicer",
		"t1": "Uživatelská příručka",
		"t2": "První výtisk",
		"t3": "Přidat podporu",
		"t4": "Připojit zařízení",
		"t5": "Bezdrátové odesílání",
		"t6": "Hlášení o problému",
		"t7": "Demonstrace",
		"t8": "Informace o produktu",
		"t9": "Kontaktujte nás",
		"t10": "Filament",
	},
	"es": {
		"t0": "Bienvenido a QIDISlicer",
		"t1": "Guía del usuario",
		"t2": "Primera de impresión",
		"t3": "Agregar soporte",
		"t4": "Conectar dispositivo",
		"t5": "Envío inalámbrico",
		"t6": "Informe de problema",
		"t7": "Demostración",
		"t8": "Información del producto",
		"t9": "Contacta con nosotros",
		"t10": "Filamento",
    },
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
